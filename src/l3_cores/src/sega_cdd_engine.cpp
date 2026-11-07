// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/sega_cdd_engine.h"

#include <cstdint>
#include <cstdlib>
#include <optional>
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

constexpr std::int64_t kSeekTicksPerSpan = 120;
constexpr std::int64_t kSeekSpanSectors = 270000;

[[nodiscard]] std::uint32_t seek_latency_ticks(SectorCount distance) noexcept {
    return static_cast<std::uint32_t>(
        (std::int64_t{distance.magnitude().v} * kSeekTicksPerSpan) / kSeekSpanSectors
    );
}

}

Ex<void> SegaCddEngine::do_notify_disc(bool loaded, const svc::DiscGeometry&) {

    st_.loaded = loaded;
    st_.status = loaded ? kCdStatStop : kCdStatNoDisc;
    if (loaded)
        st_.latency = 10;
    return {};
}

bool SegaCddEngine::do_streaming(const svc::DiscGeometry& geom) const noexcept {

    if (st_.status != kCdStatPlay || st_.latency != 0)
        return false;
    const svc::Toc& toc = geom.toc;
    if (!toc.last.contains(st_.track))
        return false;
    const bool cdda = toc.at(st_.track).type == TrackType::Cdda;

    return facts_.profile.stream_on_data_track ? !cdda : cdda;
}

Ex<void> SegaCddEngine::do_push_sector(CdDialectPorts& p) {
    if (st_.status != kCdStatPlay || st_.latency != 0 || !p.geom.toc.last.contains(st_.track)) {
        return {};
    }
    return send_sector_(p);
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

Ex<void> SegaCddEngine::do_accept(std::span<const std::uint16_t> words, CdDialectPorts& p) {
    const std::uint16_t w0 = words[0], w1 = words[1], w2 = words[2];

    if (take_reset_request() || w0 == 0x00FFu)
        do_reset_machine();

    speed_index_ = static_cast<std::uint8_t>((w2 >> 8) & 0x3u);
    const std::uint64_t c = cdwire::sega_compose(w0, w1, w2);
    for (std::size_t i = 0; i < 10; ++i) {
        st_.comm[i] = static_cast<std::uint8_t>((c >> (4 * i)) & 0xFu);
    }
    if (!cdwire::sega_command_ok(c, facts_.profile.crc_start)) {
        ++stats_.crc_faults;
    }
    command_exec_(p.geom);
    st_.has_command = true;
    return {};
}

Ex<void> SegaCddEngine::do_tick(CdDialectPorts& p) {

    if (st_.has_command) {
        const std::uint64_t frame = st_.stat.packed(
            st_.is_data,
            facts_.profile.crc_start,
            facts_.profile.status_carries_is_data
        );
        if (auto r = send_frame_words_(frame, p.link); !r)
            return r;
        st_.has_command = false;
    }
    return update_(p);
}

bool SegaCddEngine::can_send_ready_(TrackType type, bool skip_mode1, ICdLink& link) {

    if (skip_mode1 && type == TrackType::Mode1)
        return true;
    const auto selector = cdwire::sega_send_data_query(type, skip_mode1);

    if (!selector)
        return true;
    return link.can_send_data(*selector);
}

Ex<void> SegaCddEngine::update_(CdDialectPorts& p) {
    auto& m = st_;
    const svc::Toc& toc = p.geom.toc;

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
        if (!toc.last.contains(m.track)) {
            m.status = kCdStatEnd;
            return {};
        }
        return send_sector_(p);
    }
    if (m.status == kCdStatScan) {
        m.lba += m.scan_offset;
        if (toc.last.contains(m.track) && m.lba >= head_of(toc.at(m.track).end)) {
            m.track += svc::TrackCount{1};
            if (toc.last.contains(m.track)) {
                m.lba = head_of(toc.at(m.track).start);
            } else {
                m.lba = head_of(toc.end);
                m.cdda.locate();
                m.status = kCdStatEnd;
                m.is_data = true;
                return {};
            }
        } else if (toc.last.contains(m.track) && m.lba < head_of(toc.at(m.track).start)) {
            if (m.track != svc::TrackIndex{}) {
                m.track -= svc::TrackCount{1};
                m.lba = head_of(toc.at(m.track).end);
            } else {
                m.lba = HeadLba{};
            }
        }
        m.cdda.locate();
        m.is_data = toc.last.contains(m.track) && toc.at(m.track).type != TrackType::Cdda;
        return {};
    }

    ++stats_.idle_ticks;
    return {};
}

Ex<void> SegaCddEngine::send_sector_(CdDialectPorts& p) {

    auto& m = st_;
    const svc::Toc& toc = p.geom.toc;
    const svc::Track& tk = toc.at(m.track);
    const bool data_track = tk.type != TrackType::Cdda;

    pump_pipeline_(p.geom);

    if (p.egress_busy()) {
        ++stats_.busy_ticks;
        return {};
    }

    if (!can_send_ready_(tk.type, facts_.profile.ready_probe_skips_mode1, p.link)) {
        ++stats_.flow_waits;
        return {};
    }

    std::size_t len = kRawFrame;
    IoIndex idx = facts_.profile.io.data;
    if (data_track) {

        const Msf msf = msf_of(m.lba + kPregapSectors);
        sec_buf_[12] = bcd(msf.m);
        sec_buf_[13] = bcd(msf.s);
        sec_buf_[14] = bcd(msf.f);
        sec_buf_[15] = 0x01;
        if (const std::optional<Lba> at = m.lba.readable()) {

            if (facts_.disc.take(
                    svc::DiscForm::UserData,
                    *at,
                    std::as_writable_bytes(std::span(sec_buf_ + 16, kUserData))
                )
                == 0) {
                ++stats_.substitutes;
            }
            facts_.disc.hint(svc::DiscForm::UserData, *(m.lba + SectorCount{1}).readable());
        }
    } else {
        if (m.lba >= head_of(tk.start)) {
            m.is_data = false;
        }

        const SectorCount sectors = m.cdda.frame_sectors();
        len = sectors.bytes(kRawFrame);
        idx = facts_.profile.io.cdda;
        if (!m.is_data) {
            const HeadLba from = m.cdda.at(m.lba);
            for (SectorCount i{}; i < sectors; i += SectorCount{1}) {
                const auto read_lba = (from + i).readable();
                if (read_lba) {
                    if (facts_.disc.take(
                            svc::DiscForm::RawFrame,
                            *read_lba,
                            std::as_writable_bytes(
                                std::span(sec_buf_ + i.bytes(kRawFrame), kRawFrame)
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
    if (p.link.egress_bound()) {
        if (auto r = p.link.submit_sector({facts_.profile.io.subcode, sub, idx, sector}); !r)
            return r;
    } else {
        if (!sub.empty()) {
            if (auto r = send_frame(p.link, facts_.profile.io.subcode, sub); !r)
                return r;
        }
        if (auto r = send_frame(p.link, idx, sector); !r)
            return r;
    }

    if (!data_track) {
        stats_.cdda_sectors += static_cast<std::uint32_t>(len / kRawFrame);
    } else {
        ++stats_.data_sectors;
    }

    m.lba += SectorCount{1};
    if (toc.last.contains(m.track) && m.lba >= head_of(toc.at(m.track).end)) {
        m.track += svc::TrackCount{1};
        m.is_data = true;
    }

    pump_pipeline_(p.geom);
    return {};
}

std::span<const std::uint8_t> SegaCddEngine::subcode_stage_() {

    auto& m = st_;
    const std::optional<Lba> sub_at = m.lba.readable();
    if (!sub_at)
        return {};
    std::uint8_t raw[kSubBytes];
    const std::size_t sub_got = facts_.disc.take(
        svc::DiscForm::Subcode,
        *sub_at,
        std::as_writable_bytes(std::span(raw, kSubBytes))
    );
    facts_.disc.hint(svc::DiscForm::Subcode, *(m.lba + SectorCount{1}).readable());
    if (sub_got == 0) {
        ++stats_.subcode_substitutes;
        return {};
    }
    const auto il = cdwire::interleave_subcode(std::span<const std::uint8_t, 96>(raw));
    for (std::size_t i = 0; i < kSubBytes; ++i)
        sub_buf_[i] = il[i];
    return std::span<const std::uint8_t>(sub_buf_, 98);
}

void SegaCddEngine::hint_at_(HeadLba lba, const svc::DiscGeometry& geom) noexcept {
    const std::optional<Lba> at = lba.readable();
    if (!at)
        return;
    const svc::Toc& toc = geom.toc;
    const svc::TrackIndex tr = track_scan(toc, lba);
    const bool data = toc.last.contains(tr) && toc.at(tr).type != TrackType::Cdda;

    prime_head(
        facts_.disc,
        static_cast<std::int32_t>(at->v),
        {.form = data ? svc::DiscForm::UserData : svc::DiscForm::RawFrame,
         .pair = !data,
         .subcode = true}
    );
}

void SegaCddEngine::pump_pipeline_(const svc::DiscGeometry& geom) noexcept {
    auto& m = st_;
    const svc::Toc& toc = geom.toc;
    const std::optional<Lba> head = m.lba.readable();
    if (toc.last == svc::TrackCount{} || !head)
        return;
    const svc::TrackIndex tr = track_scan(toc, m.lba);
    if (toc.last.contains(tr) && toc.at(tr).type != TrackType::Cdda) {
        facts_.disc.hint(svc::DiscForm::UserData, *head);
    } else if (const std::optional<Lba> from = m.cdda.at(m.lba).readable()) {
        facts_.disc.hint(svc::DiscForm::RawFrame, *from);
        if (const std::optional<Lba> next = m.cdda.at(m.lba + SectorCount{1}).readable()) {
            facts_.disc.hint(svc::DiscForm::RawFrame, *next);
        }
    }
    facts_.disc.hint(svc::DiscForm::Subcode, *head);
}

void SegaCddEngine::seek_(HeadLba lba, bool play, const svc::DiscGeometry& geom) {

    auto& m = st_;
    const svc::Toc& toc = geom.toc;
    m.latency = (play ? 11u : 0u) + seek_latency_ticks(lba - m.lba);
    m.lba = lba;
    m.track = track_scan(toc, lba);
    hint_at_(lba, geom);

    m.cdda.locate();
    if (play)
        m.cdda.reprime();
}

void SegaCddEngine::do_reset_machine() noexcept {

    auto& m = st_;
    m.latency = 10;
    m.track = svc::TrackIndex{};
    m.lba = HeadLba{};
    m.cdda.locate();
    m.cdda.reprime();
    m.scan_offset = SectorCount{};
    m.is_data = true;
    m.status = kCdStatStop;
    m.stat = SegaCddStat{};
}

void SegaCddEngine::command_exec_(const svc::DiscGeometry& geom) {

    auto& m = st_;
    const svc::Toc& toc = geom.toc;
    using Field = SegaCddStat::Field;
    const auto cur_type_nib = [&]() -> std::uint8_t {
        return toc.last.contains(m.track) ? static_cast<std::uint8_t>(toc.at(m.track).type) : 0u;
    };
    const auto write_msf = [&](HeadLba lba) {
        m.stat.set_msf(msf_of(lba));
    };
    const auto write_absent_track = [&]() {
        m.stat.set(Field::Bcd0Tens, SegaCddStat::kAbsent);
        m.stat.set(Field::Bcd0Ones, SegaCddStat::kAbsent);
    };
    const auto write_track = [&]() {
        if (toc.last.contains(m.track)) {
            m.stat.set_bcd(SegaCddStat::BcdSlot::Bcd0, m.track.number());
        } else {
            write_absent_track();
        }
    };

    switch (m.comm[0]) {
        case kCdCommIdle:

            if (m.latency <= 3) {
                m.stat.set_status(m.status);
                if (m.stat.subcommand() == 0x0F) {
                    m.stat.set_subcommand(0x0);
                    write_msf(m.lba + kPregapSectors);
                    m.stat.set_type_byte(cur_type_nib() != 0 ? 0x04 : 0x00);
                } else if (m.stat.subcommand() == 0x00) {
                    write_msf(m.lba + kPregapSectors);
                    m.stat.set_type_byte(cur_type_nib() != 0 ? 0x04 : 0x00);
                } else if (m.stat.subcommand() == 0x01) {
                    const HeadLba base = toc.last.contains(m.track) ? head_of(toc.at(m.track).start)
                                                                    : HeadLba{};
                    write_msf(HeadLba{} + (m.lba - base).magnitude());
                    m.stat.set_type_byte(cur_type_nib() != 0 ? 0x04 : 0x00);
                } else if (m.stat.subcommand() == 0x02) {
                    write_track();
                }
            }
            break;

        case kCdCommStop:
            m.status = kCdStatStop;
            m.is_data = true;
            m.stat.set_status(m.status);
            m.stat.clear_from(Field::Subcommand);
            break;

        case kCdCommToc: {
            if (m.status == kCdStatStop)
                m.status = kCdStatToc;
            switch (m.comm[3]) {
                case 0:
                    m.stat.set_status(m.status);
                    m.stat.set_subcommand(0x0);
                    write_msf(m.lba + kPregapSectors);
                    m.stat.set_type_byte(static_cast<std::uint8_t>(cur_type_nib() << 2));
                    break;
                case 1: {
                    const HeadLba base = toc.last.contains(m.track) ? head_of(toc.at(m.track).start)
                                                                    : HeadLba{};
                    m.stat.set_status(m.status);
                    m.stat.set_subcommand(0x1);
                    write_msf(HeadLba{} + (m.lba - base).magnitude());
                    m.stat.set_type_byte(static_cast<std::uint8_t>(cur_type_nib() << 2));
                    break;
                }
                case 2:
                    m.stat.set_status(m.status);
                    m.stat.set_subcommand(0x2);
                    write_track();
                    m.stat.clear_from(Field::Bcd1Tens);
                    break;
                case 3:
                    m.stat.set_status(m.status);
                    m.stat.set_subcommand(0x3);
                    write_msf(head_of(toc.end) + kPregapSectors);
                    m.stat.set_type_byte(0);
                    break;
                case 4:
                    m.stat.set_status(m.status);
                    m.stat.set_subcommand(0x4);
                    m.stat.set(Field::Bcd0Tens, 0);
                    m.stat.set(Field::Bcd0Ones, 1);
                    m.stat.set_bcd(SegaCddStat::BcdSlot::Bcd1, toc.last);
                    m.stat.clear_from(Field::Bcd2Tens);
                    break;
                case 5: {

                    const svc::TrackNumber track = toc.last.clamped_number(
                        static_cast<std::uint32_t>(m.comm[4]) * 10u + m.comm[5]
                    );
                    const svc::Track& tk = toc.at(track.index());
                    const Msf msf = msf_of(head_of(tk.start) + kPregapSectors);
                    m.stat.set_status(m.status);
                    m.stat.set_subcommand(0x5);
                    m.stat.set_bcd(SegaCddStat::BcdSlot::Bcd0, msf.m);
                    m.stat.set_bcd(SegaCddStat::BcdSlot::Bcd1, msf.s);
                    const std::uint8_t f_packed = cdwire::bcd(msf.f);
                    m.stat.set(
                        Field::Bcd2Tens,
                        static_cast<std::uint8_t>(
                            (f_packed >> 4) | (static_cast<std::uint8_t>(tk.type) << 3)
                        )
                    );
                    m.stat.set(
                        Field::Bcd2Ones,
                        static_cast<std::uint8_t>(f_packed & SegaCddStat::kNibbleMask)
                    );
                    m.stat.set(
                        Field::Type,
                        static_cast<std::uint8_t>(cdwire::bcd(track) & SegaCddStat::kNibbleMask)
                    );
                    break;
                }
                case 6:
                    m.stat.set_status(m.status);
                    m.stat.set_subcommand(0x6);
                    m.stat.clear_from(Field::Bcd0Tens);
                    break;
                default:
                    break;
            }
            break;
        }

        case kCdCommPlay:
        case kCdCommSeek: {

            const HeadLba lba = HeadLba{static_cast<std::int32_t>(
                                    (static_cast<std::uint32_t>(m.comm[2]) * 10u + m.comm[3])
                                        * svc::kSecondsPerMinute
                                        * svc::kFramesPerSecond
                                    + (static_cast<std::uint32_t>(m.comm[4]) * 10u + m.comm[5])
                                        * svc::kFramesPerSecond
                                    + (static_cast<std::uint32_t>(m.comm[6]) * 10u + m.comm[7])
                                )}
                - kPregapSectors;
            const bool play = m.comm[0] == kCdCommPlay;
            seek_(lba, play, geom);
            m.is_data = true;
            m.status = play ? kCdStatPlay : kCdStatSeek;
            m.stat.set_status(kCdStatSeek);
            m.stat.set_subcommand(0xF);
            m.stat.clear_from(Field::Bcd0Tens);
            break;
        }

        case kCdCommPause:
            m.is_data = true;
            m.status = kCdStatPause;
            m.stat.set_status(m.status);
            break;

        case kCdCommResume:
            m.status = kCdStatPlay;
            m.stat.set_status(m.status);
            m.cdda.reprime();
            break;

        case kCdCommFwScan:
            m.scan_offset = SectorCount{kCdScanSpeed};
            m.status = kCdStatScan;
            m.stat.set_status(m.status);
            break;

        case kCdCommRwScan:
            m.scan_offset = SectorCount{-kCdScanSpeed};
            m.status = kCdStatScan;
            m.stat.set_status(m.status);
            break;

        case kCdCommTrackMove:

            m.is_data = true;
            m.status = kCdStatPause;
            m.stat.set_status(m.status);
            break;

        case kCdCommTrackPlay: {

            const svc::TrackIndex row = toc.last
                                            .clamped_number(
                                                static_cast<std::uint32_t>(m.comm[2]) * 10u
                                                + m.comm[3]
                                            )
                                            .index();
            const HeadLba lba = head_of(toc.at(row).start);
            seek_(lba, true, geom);
            m.is_data = true;
            m.status = kCdStatPlay;
            m.stat.set_status(kCdStatSeek);
            m.stat.set_subcommand(0xF);
            m.stat.clear_from(Field::Bcd0Tens);
            break;
        }

        case kCdCommTrayClose:
            m.is_data = true;
            m.status = m.loaded ? kCdStatToc : kCdStatNoDisc;
            m.stat.set_status(kCdStatStop);
            break;

        case kCdCommTrayOpen:
            m.is_data = true;
            m.status = kCdStatOpen;
            m.stat.set_status(kCdStatOpen);
            break;

        default:
            m.stat.set_status(m.status);
            break;
    }
}

}
