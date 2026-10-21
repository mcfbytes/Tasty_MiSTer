// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/sega_cdd_engine.h"

#include <cstdint>
#include <cstdlib>
#include <span>

#include "cd_core_detail.h"
#include "cores/cd_head_prime.h"
#include "cores/cdwire.h"
#include "infra/byte_view.h"
#include "infra/error.h"

namespace mister::cores {

namespace {

constexpr std::uint8_t kCdStatStop = 0x0;
constexpr std::uint8_t kCdStatPlay = 0x1;
constexpr std::uint8_t kCdStatSeek = 0x2;
constexpr std::uint8_t kCdStatScan = 0x3;
constexpr std::uint8_t kCdStatPause = 0x4;
constexpr std::uint8_t kCdStatOpen = 0x5;
constexpr std::uint8_t kCdStatToc = 0x9;
constexpr std::uint8_t kCdStatNoDisc = 0xB;
constexpr std::uint8_t kCdStatEnd = 0xC;

constexpr std::uint8_t kCdCommIdle = 0x0;
constexpr std::uint8_t kCdCommStop = 0x1;
constexpr std::uint8_t kCdCommToc = 0x2;
constexpr std::uint8_t kCdCommPlay = 0x3;
constexpr std::uint8_t kCdCommSeek = 0x4;
constexpr std::uint8_t kCdCommPause = 0x6;
constexpr std::uint8_t kCdCommResume = 0x7;
constexpr std::uint8_t kCdCommFwScan = 0x8;
constexpr std::uint8_t kCdCommRwScan = 0x9;
constexpr std::uint8_t kCdCommTrackMove = 0xA;
constexpr std::uint8_t kCdCommTrackPlay = 0xB;
constexpr std::uint8_t kCdCommTrayClose = 0xC;
constexpr std::uint8_t kCdCommTrayOpen = 0xD;

constexpr std::int32_t kCdScanSpeed = 30;

}

Ex<void> SegaCddEngine::do_notify_disc(bool loaded) {

    st_.loaded = loaded;
    st_.status = loaded ? kCdStatStop : kCdStatNoDisc;
    if (loaded)
        st_.latency = 10;
    return {};
}

bool SegaCddEngine::do_streaming() const noexcept {

    if (st_.status != kCdStatPlay || st_.latency != 0)
        return false;
    const svc::Toc& toc = ports_.geom.toc;
    if (st_.track >= toc.last)
        return false;
    const bool cdda = toc.tracks[st_.track].type == TrackType::Cdda;

    return ports_.profile.stream_on_data_track ? !cdda : cdda;
}

Ex<void> SegaCddEngine::do_push_sector() {
    if (st_.status != kCdStatPlay || st_.latency != 0 || st_.track >= ports_.geom.toc.last) {
        return {};
    }
    return send_sector_();
}

std::size_t SegaCddEngine::do_status_words(
    std::uint64_t frame,
    std::span<std::uint16_t, CdEngine::kMaxStatusWords> out
) const noexcept {
    const auto w = cdwire::sega_set_words(frame);
    out[0] = w[0];
    out[1] = w[1];
    out[2] = w[2];
    return 3;
}

Ex<void> SegaCddEngine::do_accept(std::span<const std::uint16_t> words) {
    const std::uint16_t w0 = words[0], w1 = words[1], w2 = words[2];

    if (take_reset_request() || w0 == 0x00FFu)
        do_reset_machine();

    speed_index_ = static_cast<std::uint8_t>((w2 >> 8) & 0x3u);
    const std::uint64_t c = cdwire::sega_compose(w0, w1, w2);
    for (std::size_t i = 0; i < 10; ++i) {
        st_.comm[i] = static_cast<std::uint8_t>((c >> (4 * i)) & 0xFu);
    }
    if (!cdwire::sega_command_ok(c, ports_.profile.crc_start)) {
        ++stats_.crc_faults;
    }
    command_exec_();
    st_.has_command = true;
    return {};
}

Ex<void> SegaCddEngine::do_tick() {

    if (st_.has_command) {
        const std::uint64_t frame = cdwire::sega_pack_status(
            std::span<const std::uint8_t, 9>(st_.stat),
            st_.is_data,
            ports_.profile.crc_start,
            ports_.profile.status_carries_is_data
        );
        if (auto r = ports_.tx.send_status(frame); !r)
            return r;
        st_.has_command = false;
    }
    return update_();
}

bool SegaCddEngine::can_send_ready_(TrackType type, bool skip_mode1) {

    if (skip_mode1 && type == TrackType::Mode1)
        return true;
    if (flow_ == nullptr)
        return true;
    const auto selector = cdwire::sega_send_data_query(type, skip_mode1);

    if (!selector)
        return true;
    return flow_->can_send_data(*selector);
}

Ex<void> SegaCddEngine::update_() {
    auto& m = st_;
    const svc::Toc& toc = ports_.geom.toc;

    if (m.status == kCdStatStop || m.status == 0x0E || m.status == kCdStatOpen) {

        if (m.latency > 0) {
            --m.latency;
            return {};
        }
        if (!m.loaded)
            m.status = kCdStatNoDisc;
        return {};
    }
    if (m.status == kCdStatSeek) {
        if (m.latency > 0) {
            --m.latency;
            return {};
        }
        m.status = kCdStatPause;
        return {};
    }
    if (m.status == kCdStatPlay) {
        if (m.latency > 0) {
            --m.latency;
            return {};
        }
        if (m.track >= toc.last) {
            m.status = kCdStatEnd;
            return {};
        }
        return send_sector_();
    }
    if (m.status == kCdStatScan) {
        m.lba += m.scan_offset;
        if (m.track < toc.last && m.lba >= static_cast<std::int64_t>(toc.tracks[m.track].end.v)) {
            ++m.track;
            if (m.track < toc.last) {
                m.lba = static_cast<std::int32_t>(toc.tracks[m.track].start.v);
            } else {
                m.lba = static_cast<std::int32_t>(toc.end.v);
                m.cdda.locate();
                m.status = kCdStatEnd;
                m.is_data = true;
                return {};
            }
        } else if (
            m.track < toc.last && m.lba < static_cast<std::int64_t>(toc.tracks[m.track].start.v)
        ) {
            if (m.track > 0) {
                --m.track;
                m.lba = static_cast<std::int32_t>(toc.tracks[m.track].end.v);
            } else {
                m.lba = 0;
            }
        }
        m.cdda.locate();
        m.is_data = m.track < toc.last && toc.tracks[m.track].type != TrackType::Cdda;
        return {};
    }

    ++stats_.idle_ticks;
    return {};
}

Ex<void> SegaCddEngine::send_sector_() {

    auto& m = st_;
    const svc::Toc& toc = ports_.geom.toc;
    const svc::Track& tk = toc.tracks[m.track];
    const bool data_track = tk.type != TrackType::Cdda;

    pump_pipeline_();

    if (ports_.egress_busy()) {
        ++stats_.busy_ticks;
        return {};
    }

    if (!can_send_ready_(tk.type, ports_.profile.ready_probe_skips_mode1)) {
        ++stats_.flow_waits;
        return {};
    }

    std::size_t len = kRawFrame;
    IoIndex idx = ports_.profile.io.data;
    if (data_track) {

        const Msf msf = lba_to_msf(m.lba + 150);
        sec_buf_[12] = bcd(msf.m);
        sec_buf_[13] = bcd(msf.s);
        sec_buf_[14] = bcd(msf.f);
        sec_buf_[15] = 0x01;
        if (m.lba >= 0) {
            const Lba at{static_cast<std::uint32_t>(m.lba)};

            if (ports_.disc.take(
                    svc::DiscForm::UserData,
                    at,
                    std::as_writable_bytes(std::span(sec_buf_ + 16, kUserData))
                )
                == 0) {
                ++stats_.substitutes;
            }
            ports_.disc.hint(svc::DiscForm::UserData, Lba{at.v + 1});
        }
    } else {
        if (m.lba >= static_cast<std::int64_t>(tk.start.v)) {
            m.is_data = false;
        }

        const std::int32_t sectors = m.cdda.frame_sectors();
        len = static_cast<std::size_t>(sectors) * kRawFrame;
        idx = ports_.profile.io.cdda;
        if (!m.is_data) {
            const std::int32_t from = m.cdda.at(m.lba);
            for (std::int32_t i = 0; i < sectors; ++i) {
                const std::int32_t read_lba = from + i;
                if (read_lba >= 0) {
                    const Lba at{static_cast<std::uint32_t>(read_lba)};
                    if (ports_.disc.take(
                            svc::DiscForm::RawFrame,
                            at,
                            std::as_writable_bytes(
                                std::span(
                                    sec_buf_ + static_cast<std::size_t>(i) * kRawFrame,
                                    kRawFrame
                                )
                            )
                        )
                        == 0) {
                        ++stats_.substitutes;
                    }
                }
            }
            m.cdda.read(sectors);
        }
    }

    const std::span<const std::uint8_t> sub = subcode_stage_();
    const std::span<const std::uint8_t> sector(sec_buf_, len);
    if (ports_.egress) {
        if (auto r = ports_.egress->submit_sector({ports_.profile.io.subcode, sub, idx, sector});
            !r)
            return r;
    } else {
        if (!sub.empty()) {
            if (auto r = send_frame(ports_.tx, ports_.profile.io.subcode, sub); !r)
                return r;
        }
        if (auto r = send_frame(ports_.tx, idx, sector); !r)
            return r;
    }

    if (!data_track) {
        stats_.cdda_sectors += static_cast<std::uint32_t>(len / kRawFrame);
    } else {
        ++stats_.data_sectors;
    }

    ++m.lba;
    if (m.track < toc.last && m.lba >= static_cast<std::int64_t>(toc.tracks[m.track].end.v)) {
        ++m.track;
        m.is_data = true;
    }

    pump_pipeline_();
    return {};
}

std::span<const std::uint8_t> SegaCddEngine::subcode_stage_() {

    auto& m = st_;
    if (m.lba < 0)
        return {};
    std::uint8_t raw[kSubBytes];
    const Lba sub_at{static_cast<std::uint32_t>(m.lba)};
    const std::size_t sub_got = ports_.disc.take(
        svc::DiscForm::Subcode,
        sub_at,
        std::as_writable_bytes(std::span(raw, kSubBytes))
    );
    ports_.disc.hint(svc::DiscForm::Subcode, Lba{sub_at.v + 1});
    if (sub_got == 0) {
        ++stats_.subcode_substitutes;
        return {};
    }
    const auto il = cdwire::interleave_subcode(std::span<const std::uint8_t, 96>(raw));
    for (std::size_t i = 0; i < kSubBytes; ++i)
        sub_buf_[i] = il[i];
    return std::span<const std::uint8_t>(sub_buf_, 98);
}

void SegaCddEngine::hint_at_(std::int32_t lba) noexcept {
    if (lba < 0)
        return;
    const svc::Toc& toc = ports_.geom.toc;
    const std::uint32_t tr = track_scan(toc, lba);
    const bool data = tr < toc.last && toc.tracks[tr].type != TrackType::Cdda;

    prime_head(
        ports_.disc,
        lba,
        {.form = data ? svc::DiscForm::UserData : svc::DiscForm::RawFrame,
         .pair = !data,
         .subcode = true}
    );
}

void SegaCddEngine::pump_pipeline_() noexcept {
    auto& m = st_;
    const svc::Toc& toc = ports_.geom.toc;
    if (toc.last == 0 || m.lba < 0)
        return;
    const Lba head{static_cast<std::uint32_t>(m.lba)};
    const std::uint32_t tr = track_scan(toc, m.lba);
    if (tr < toc.last && toc.tracks[tr].type != TrackType::Cdda) {
        ports_.disc.hint(svc::DiscForm::UserData, head);
    } else {
        const std::int32_t from = m.cdda.at(m.lba);
        if (from >= 0) {
            const Lba base{static_cast<std::uint32_t>(from)};
            ports_.disc.hint(svc::DiscForm::RawFrame, base);
            ports_.disc.hint(svc::DiscForm::RawFrame, Lba{base.v + 1});
        }
    }
    ports_.disc.hint(svc::DiscForm::Subcode, head);
}

void SegaCddEngine::seek_(std::int32_t lba, bool play) {

    auto& m = st_;
    const svc::Toc& toc = ports_.geom.toc;
    m.latency = (play ? 11u : 0u)
        + static_cast<std::uint32_t>((std::abs(lba - m.lba) * std::int64_t{120}) / 270000);
    m.lba = lba;
    m.track = track_scan(toc, lba);
    hint_at_(lba);

    m.cdda.locate();
    if (play)
        m.cdda.reprime();
}

void SegaCddEngine::do_reset_machine() noexcept {

    auto& m = st_;
    m.latency = 10;
    m.track = 0;
    m.lba = 0;
    m.cdda.locate();
    m.cdda.reprime();
    m.scan_offset = 0;
    m.is_data = true;
    m.status = kCdStatStop;
    for (auto& s : m.stat)
        s = 0;
}

void SegaCddEngine::command_exec_() {

    auto& m = st_;
    const svc::Toc& toc = ports_.geom.toc;
    const auto cur_type_nib = [&]() -> std::uint8_t {
        return (m.track < toc.last) ? static_cast<std::uint8_t>(toc.tracks[m.track].type) : 0u;
    };
    const auto write_msf = [&](std::int32_t lba) {
        const Msf msf = lba_to_msf(lba);
        m.stat[2] = static_cast<std::uint8_t>(bcd(msf.m) >> 4);
        m.stat[3] = static_cast<std::uint8_t>(bcd(msf.m) & 0xF);
        m.stat[4] = static_cast<std::uint8_t>(bcd(msf.s) >> 4);
        m.stat[5] = static_cast<std::uint8_t>(bcd(msf.s) & 0xF);
        m.stat[6] = static_cast<std::uint8_t>(bcd(msf.f) >> 4);
        m.stat[7] = static_cast<std::uint8_t>(bcd(msf.f) & 0xF);
    };
    const auto zero_tail = [&](std::size_t from) {
        for (std::size_t i = from; i < 9; ++i)
            m.stat[i] = 0;
    };

    switch (m.comm[0]) {
        case kCdCommIdle:

            if (m.latency <= 3) {
                m.stat[0] = m.status;
                if (m.stat[1] == 0x0F) {
                    m.stat[1] = 0x0;
                    write_msf(m.lba + 150);
                    m.stat[8] = cur_type_nib() != 0 ? 0x04 : 0x00;
                } else if (m.stat[1] == 0x00) {
                    write_msf(m.lba + 150);
                    m.stat[8] = cur_type_nib() != 0 ? 0x04 : 0x00;
                } else if (m.stat[1] == 0x01) {
                    const std::int32_t base = (m.track < toc.last)
                        ? static_cast<std::int32_t>(toc.tracks[m.track].start.v)
                        : 0;
                    write_msf(std::abs(m.lba - base));
                    m.stat[8] = cur_type_nib() != 0 ? 0x04 : 0x00;
                } else if (m.stat[1] == 0x02) {
                    const bool in = m.track < toc.last;
                    m.stat[2] = in ? static_cast<std::uint8_t>(bcd(m.track + 1) >> 4) : 0xA;
                    m.stat[3] = in ? static_cast<std::uint8_t>(bcd(m.track + 1) & 0xF) : 0xA;
                }
            }
            break;

        case kCdCommStop:
            m.status = kCdStatStop;
            m.is_data = true;
            m.stat[0] = m.status;
            zero_tail(1);
            break;

        case kCdCommToc: {
            if (m.status == kCdStatStop)
                m.status = kCdStatToc;
            switch (m.comm[3]) {
                case 0:
                    m.stat[0] = m.status;
                    m.stat[1] = 0x0;
                    write_msf(m.lba + 150);
                    m.stat[8] = static_cast<std::uint8_t>(cur_type_nib() << 2);
                    break;
                case 1: {
                    const std::int32_t base = (m.track < toc.last)
                        ? static_cast<std::int32_t>(toc.tracks[m.track].start.v)
                        : 0;
                    m.stat[0] = m.status;
                    m.stat[1] = 0x1;
                    write_msf(std::abs(m.lba - base));
                    m.stat[8] = static_cast<std::uint8_t>(cur_type_nib() << 2);
                    break;
                }
                case 2: {
                    const bool in = m.track < toc.last;
                    m.stat[0] = m.status;
                    m.stat[1] = 0x2;
                    m.stat[2] = in ? static_cast<std::uint8_t>(bcd(m.track + 1) >> 4) : 0xA;
                    m.stat[3] = in ? static_cast<std::uint8_t>(bcd(m.track + 1) & 0xF) : 0xA;
                    zero_tail(4);
                    break;
                }
                case 3:
                    m.stat[0] = m.status;
                    m.stat[1] = 0x3;
                    write_msf(static_cast<std::int32_t>(toc.end.v) + 150);
                    m.stat[8] = 0;
                    break;
                case 4:
                    m.stat[0] = m.status;
                    m.stat[1] = 0x4;
                    m.stat[2] = 0;
                    m.stat[3] = 1;
                    m.stat[4] = static_cast<std::uint8_t>(bcd(toc.last) >> 4);
                    m.stat[5] = static_cast<std::uint8_t>(bcd(toc.last) & 0xF);
                    zero_tail(6);
                    break;
                case 5: {

                    std::uint32_t track = static_cast<std::uint32_t>(m.comm[4]) * 10u + m.comm[5];
                    const std::uint32_t last = toc.last ? toc.last : 1u;
                    if (track < 1)
                        track = 1;
                    if (track > last)
                        track = last;
                    const auto& tk = toc.tracks[track - 1];
                    const Msf msf = lba_to_msf(static_cast<std::int32_t>(tk.start.v) + 150);
                    m.stat[0] = m.status;
                    m.stat[1] = 0x5;
                    m.stat[2] = static_cast<std::uint8_t>(bcd(msf.m) >> 4);
                    m.stat[3] = static_cast<std::uint8_t>(bcd(msf.m) & 0xF);
                    m.stat[4] = static_cast<std::uint8_t>(bcd(msf.s) >> 4);
                    m.stat[5] = static_cast<std::uint8_t>(bcd(msf.s) & 0xF);
                    m.stat[6] = static_cast<std::uint8_t>(
                        (bcd(msf.f) >> 4) | (static_cast<std::uint8_t>(tk.type) << 3)
                    );
                    m.stat[7] = static_cast<std::uint8_t>(bcd(msf.f) & 0xF);
                    m.stat[8] = static_cast<std::uint8_t>(bcd(track) & 0xF);
                    break;
                }
                case 6:
                    m.stat[0] = m.status;
                    m.stat[1] = 0x6;
                    zero_tail(2);
                    break;
                default:
                    break;
            }
            break;
        }

        case kCdCommPlay:
        case kCdCommSeek: {

            const std::int32_t lba = static_cast<std::int32_t>(
                                         (static_cast<std::uint32_t>(m.comm[2]) * 10u + m.comm[3])
                                             * 60u
                                             * 75u
                                         + (static_cast<std::uint32_t>(m.comm[4]) * 10u + m.comm[5])
                                             * 75u
                                         + (static_cast<std::uint32_t>(m.comm[6]) * 10u + m.comm[7])
                                     )
                - 150;
            const bool play = m.comm[0] == kCdCommPlay;
            seek_(lba, play);
            m.is_data = true;
            m.status = play ? kCdStatPlay : kCdStatSeek;
            m.stat[0] = kCdStatSeek;
            m.stat[1] = 0xF;
            zero_tail(2);
            break;
        }

        case kCdCommPause:
            m.is_data = true;
            m.status = kCdStatPause;
            m.stat[0] = m.status;
            break;

        case kCdCommResume:
            m.status = kCdStatPlay;
            m.stat[0] = m.status;
            m.cdda.reprime();
            break;

        case kCdCommFwScan:
            m.scan_offset = kCdScanSpeed;
            m.status = kCdStatScan;
            m.stat[0] = m.status;
            break;

        case kCdCommRwScan:
            m.scan_offset = -kCdScanSpeed;
            m.status = kCdStatScan;
            m.stat[0] = m.status;
            break;

        case kCdCommTrackMove:

            m.is_data = true;
            m.status = kCdStatPause;
            m.stat[0] = m.status;
            break;

        case kCdCommTrackPlay: {
            std::uint32_t index = static_cast<std::uint32_t>(m.comm[2]) * 10u + m.comm[3];
            if (index > 0)
                index -= 1;

            index = (toc.last == 0) ? 0u : (index >= toc.last) ? toc.last - 1u : index;
            const std::int32_t lba = static_cast<std::int32_t>(toc.tracks[index].start.v);
            seek_(lba, true);
            m.is_data = true;
            m.status = kCdStatPlay;
            m.stat[0] = kCdStatSeek;
            m.stat[1] = 0xF;
            zero_tail(2);
            break;
        }

        case kCdCommTrayClose:
            m.is_data = true;
            m.status = m.loaded ? kCdStatToc : kCdStatNoDisc;
            m.stat[0] = kCdStatStop;
            break;

        case kCdCommTrayOpen:
            m.is_data = true;
            m.status = kCdStatOpen;
            m.stat[0] = kCdStatOpen;
            break;

        default:
            m.stat[0] = m.status;
            break;
    }
}

}
