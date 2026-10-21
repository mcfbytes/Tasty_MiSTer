// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/pce_scsi_engine.h"

#include <cstdint>
#include <span>

#include "cd_core_detail.h"
#include "cores/cd_engine.h"
#include "cores/cd_head_prime.h"
#include "cores/cdwire.h"
#include "infra/byte_view.h"
#include "infra/error.h"
#include "proto/spi_fio_queue.h"

namespace mister::cores {

namespace {

constexpr std::uint8_t kPceStateIdle = 0;
constexpr std::uint8_t kPceStateNoDisc = 1;
constexpr std::uint8_t kPceStateRead = 2;
constexpr std::uint8_t kPceStatePlay = 3;
constexpr std::uint8_t kPceStatePause = 4;
constexpr std::uint16_t kPceStatusGood = 0;
constexpr std::uint16_t kPceStatusCheckCond = 1;

constexpr std::uint8_t kPceCommTestUnit = 0x00;
constexpr std::uint8_t kPceCommRequestSense = 0x03;
constexpr std::uint8_t kPceCommRead6 = 0x08;
constexpr std::uint8_t kPceCommModeSelect6 = 0x15;
constexpr std::uint8_t kPceCommSapsp = 0xD8;
constexpr std::uint8_t kPceCommSapep = 0xD9;
constexpr std::uint8_t kPceCommPause = 0xDA;
constexpr std::uint8_t kPceCommReadSubq = 0xDD;
constexpr std::uint8_t kPceCommGetDirInfo = 0xDE;

constexpr std::uint8_t kSenseNotReady = 0x2;
constexpr std::uint8_t kSenseIllegalRequest = 0x5;
constexpr std::uint8_t kNseNoDisc = 0x0B;
constexpr std::uint8_t kNseInvalidCommand = 0x20;
constexpr std::uint8_t kPceCddaModeSilent = 0x00;
constexpr std::uint8_t kPceCddaModeLoop = 0x01;
constexpr std::uint8_t kPceCddaModeInterrupt = 0x02;

constexpr std::size_t kPceCddaFrame = kRawFrame + 2;
constexpr std::size_t kPceSubFrame = kSubBytes + 4;
constexpr std::size_t kStatusCommandWords = 1 + CdEngine::kMaxStatusWords;
static_assert(
    2 * (kPceCddaFrame + kPceSubFrame) + 12 + 2 * 2 * kStatusCommandWords
        <= proto::SpiFioQueue::kSlabBytes,
    "the doubled CD-DA pass and its follow-ups fit the slab"
);
static_assert(kStatusCommandWords <= proto::SpiFioQueue::kMaxCommandWords);

constexpr std::size_t pce_widest_windows(std::size_t piece_bytes) {
    using Q = proto::SpiFioQueue;
    return 2
        * (Q::windows_for(kPceCddaFrame, piece_bytes) + Q::windows_for(kPceSubFrame, piece_bytes))
        + Q::windows_for(12, piece_bytes)
        + 2;
}

static_assert(
    pce_widest_windows(2 * proto::SpiFioQueue::kPieceWords) <= proto::SpiFioQueue::kWindows
        && pce_widest_windows(2 * 294) <= proto::SpiFioQueue::kWindows,
    "the doubled CD-DA pass and its follow-ups fit the window ring at both pieces"
);

constexpr std::uint8_t u8_from_bcd(std::uint8_t v) noexcept {
    return static_cast<std::uint8_t>(static_cast<unsigned>((v >> 4) * 10u) + (v & 0x0Fu));
}

struct Msf8 {
    std::uint8_t m = 0, s = 0, f = 0;
};

constexpr Msf8 lba_to_msf_rel(std::int32_t lba) noexcept {
    return Msf8{
        static_cast<std::uint8_t>((lba / 75) / 60),
        static_cast<std::uint8_t>((lba / 75) % 60),
        static_cast<std::uint8_t>(lba % 75)
    };
}

}

Ex<void> PceScsiEngine::do_notify_disc(bool loaded) {

    st_.loaded = loaded;
    st_.state = loaded ? kPceStateIdle : kPceStateNoDisc;
    if (loaded)
        st_.latency = 10;
    return {};
}

bool PceScsiEngine::do_streaming() const noexcept {

    return st_.latency == 0 && st_.state == kPceStateRead;
}

Ex<void> PceScsiEngine::do_push_sector() {
    if (st_.state != kPceStateRead || st_.latency != 0 || st_.track >= ports_.geom.toc.last) {
        return {};
    }
    return read_send_();
}

std::size_t PceScsiEngine::do_status_words(
    std::uint64_t frame,
    std::span<std::uint16_t, CdEngine::kMaxStatusWords> out
) const noexcept {

    out[0] = static_cast<std::uint16_t>(frame & 0xFFFFu);
    out[1] = static_cast<std::uint16_t>((frame >> 16) & 0xFFFFu);
    return 2;
}

Ex<void> PceScsiEngine::do_accept(std::span<const std::uint16_t> w) {

    switch (w[6] & 0xFFu) {
        case 0:

            for (std::size_t i = 0; i < 7; ++i) {
                st_.comm[2 * i] = static_cast<std::uint8_t>(w[i] & 0xFFu);
                st_.comm[2 * i + 1] = static_cast<std::uint8_t>(w[i] >> 8);
            }
            return command_exec_();
        case 1:

            return send_status_(0);
        case 2:
            st_.can_read_next = true;
            break;
        default:
            request_reset();
            break;
    }
    return {};
}

Ex<void> PceScsiEngine::send_status_(std::uint16_t status) {
    const auto w = cdwire::pce_set_words(status, st_.region, false);
    return ports_.tx.send_status(
        static_cast<std::uint64_t>(w[0]) | (static_cast<std::uint64_t>(w[1]) << 16)
    );
}

Ex<void> PceScsiEngine::send_data_request_() {
    const auto w = cdwire::pce_set_words(0, st_.region, true);
    return ports_.tx.send_status(
        static_cast<std::uint64_t>(w[0]) | (static_cast<std::uint64_t>(w[1]) << 16)
    );
}

std::uint32_t PceScsiEngine::track_by_lba_(std::int32_t lba) const {
    return track_scan(ports_.geom.toc, lba);
}

std::uint32_t PceScsiEngine::seek_ticks_(std::int32_t from, std::int32_t to) const {

    if (from < 0 || to < 0)
        return 0;
    const std::uint32_t ms = ports_.geom.seek_ms(
        Lba{static_cast<std::uint32_t>(from)},
        Lba{static_cast<std::uint32_t>(to)}
    );
    return static_cast<std::uint32_t>(static_cast<float>(ms) / 13.33f);
}

Ex<bool> PceScsiEngine::drain_pending() {

    auto& m = st_;
    if (m.has_status && m.latency == 0) {
        if (auto r = send_status_(m.stat); !r)
            return std::unexpected(r.error());
        m.has_status = false;
        return true;
    }
    if (m.data_req && m.latency == 0) {
        if (auto r = send_data_request_(); !r)
            return std::unexpected(r.error());
        m.data_req = false;
        return true;
    }
    return false;
}

Ex<void> PceScsiEngine::do_tick() {

    if (auto r = drain_pending(); !r)
        return std::unexpected(r.error());
    return update_();
}

void PceScsiEngine::hint_at_(std::int32_t lba, bool doubled) noexcept {
    if (lba < 0)
        return;
    const svc::Toc& toc = ports_.geom.toc;
    const std::uint32_t tr = track_by_lba_(lba);
    const bool data = tr < toc.last && toc.tracks[tr].type != TrackType::Cdda;

    prime_head(
        ports_.disc,
        lba,
        {.form = data ? svc::DiscForm::UserData : svc::DiscForm::RawFrame,
         .pair = doubled && !data,
         .subcode = true}
    );
}

void PceScsiEngine::build_subcode_frame_(std::int32_t lba) {

    auto& m = st_;
    if (lba != m.subc_lba && m.latency == 0) {
        bool have = false;
        if (lba >= 0) {
            const Lba at{static_cast<std::uint32_t>(lba)};
            have = ports_.disc.take(
                       svc::DiscForm::Subcode,
                       at,
                       std::as_writable_bytes(std::span(m.subc, kSubBytes))
                   )
                != 0;
            ports_.disc.hint(svc::DiscForm::Subcode, Lba{at.v + 1});
        }
        if (!have) {
            if (lba >= 0)
                ++stats_.subcode_substitutes;

            for (auto& b : m.subc)
                b = 0;
            m.subc[12] = 1;
            m.subc[13] = bcd(m.track + 1);
            m.subc[14] = 1;
            const svc::Toc& toc = ports_.geom.toc;

            const auto base = static_cast<std::int32_t>(toc.tracks[m.track].start.v);

            const Msf8 rel = lba_to_msf_rel(m.lba - base);
            m.subc[15] = bcd(rel.m);
            m.subc[16] = bcd(rel.s);
            m.subc[17] = bcd(rel.f);
            const Msf am = lba_to_msf(m.lba + 150);
            m.subc[19] = bcd(am.m);
            m.subc[20] = bcd(am.s);
            m.subc[21] = bcd(am.f);

            std::uint8_t msb = 0, lsb = 0;
            for (std::size_t i = 12; i < 22; ++i) {
                std::uint8_t x = static_cast<std::uint8_t>(m.subc[i] ^ msb);
                x = static_cast<std::uint8_t>(x ^ (x >> 4));
                msb = static_cast<std::uint8_t>(lsb ^ (x >> 3) ^ static_cast<std::uint8_t>(x << 4));
                lsb = static_cast<std::uint8_t>(x ^ static_cast<std::uint8_t>(x << 5));
            }
            m.subc[22] = static_cast<std::uint8_t>(msb ^ 0xFF);
            m.subc[23] = static_cast<std::uint8_t>(lsb ^ 0xFF);
        }
        m.subc_lba = lba;
    }
    sub_buf_[0] = 0x62;
    sub_buf_[1] = 0x00;
    sub_buf_[2] = 0x00;
    sub_buf_[3] = 0x80;
    const auto il = cdwire::interleave_subcode(std::span<const std::uint8_t, 96>(m.subc));
    for (std::size_t i = 0; i < kSubBytes; ++i)
        sub_buf_[4 + i] = il[i];
}

Ex<void> PceScsiEngine::read_send_() {

    auto& m = st_;
    const svc::Toc& toc = ports_.geom.toc;
    if (!m.can_read_next)
        return {};

    if (ports_.egress_busy()) {
        ++stats_.busy_ticks;
        return {};
    }
    m.can_read_next = false;

    const svc::Track& tk = toc.tracks[m.track];
    if (tk.type != TrackType::Cdda) {
        sec_buf_[0] = 0x00;
        sec_buf_[1] = 0x08 | 0x80;
        if (m.lba >= 0) {
            const Lba at{static_cast<std::uint32_t>(m.lba)};
            if (ports_.disc.take(
                    svc::DiscForm::UserData,
                    at,
                    std::as_writable_bytes(std::span(sec_buf_ + 2, kUserData))
                )
                == 0) {
                ++stats_.substitutes;
            }
            ports_.disc.hint(svc::DiscForm::UserData, Lba{at.v + 1});
        }
        if (auto r = send_frame(
                ports_.tx,
                ports_.profile.io.data,
                std::span(sec_buf_, kUserData + 2)
            );
            !r) {
            return r;
        }
        ++stats_.data_sectors;
    } else if (m.lba >= static_cast<std::int64_t>(tk.start.v)) {
        m.is_data = false;
    }

    --m.cnt;
    if (m.cnt == 0) {
        m.stat = kPceStatusGood;
        m.has_status = true;
        m.state = kPceStateIdle;
    }
    ++m.lba;
    if (m.track < toc.last && m.lba >= static_cast<std::int64_t>(toc.tracks[m.track].end.v)) {
        ++m.track;
        m.is_data = true;
    }
    return {};
}

void PceScsiEngine::pump_pipeline_() noexcept {
    auto& m = st_;
    const svc::Toc& toc = ports_.geom.toc;
    if (toc.last == 0 || m.lba < 0)
        return;
    const std::uint32_t tr = track_by_lba_(m.lba);
    const bool data = tr < toc.last && toc.tracks[tr].type != TrackType::Cdda;

    prime_head(
        ports_.disc,
        m.lba,
        {.form = data ? svc::DiscForm::UserData : svc::DiscForm::RawFrame, .subcode = true}
    );
}

Ex<void> PceScsiEngine::update_() {
    auto& m = st_;
    const svc::Toc& toc = ports_.geom.toc;

    pump_pipeline_();

    m.subq[0] = 0x0A;
    m.subq[1] = 0x00 | 0x80;
    m.subq[2] = static_cast<std::uint8_t>(
        m.state == kPceStatePause ? 2 : (m.state == kPceStatePlay ? 0 : 3)
    );
    m.subq[3] = 0;
    m.subq[4] = bcd(m.track + 1);
    m.subq[5] = 1;
    {

        const auto base = static_cast<std::int32_t>(toc.tracks[m.track].start.v);

        const Msf8 rel = lba_to_msf_rel(m.lba - base);
        m.subq[6] = bcd(rel.m);
        m.subq[7] = bcd(rel.s);
        m.subq[8] = bcd(rel.f);
        const Msf am = lba_to_msf(m.lba + 150);
        m.subq[9] = bcd(am.m);
        m.subq[10] = bcd(am.s);
        m.subq[11] = bcd(am.f);
    }

    bool subq_send = false;

    if (m.state == kPceStateRead) {

        if (m.latency > 0) {
            --m.latency;
            return {};
        }
        if (m.track >= toc.last) {
            m.state = kPceStateIdle;
            return {};
        }
        return read_send_();
    }
    if (m.state == kPceStatePlay) {
        bool skip = false;
        if (m.latency > 0) {
            --m.latency;
            skip = true;
        } else if (m.audio_delay > 0) {
            --m.audio_delay;
            skip = true;
        } else if (ports_.egress_busy()) {
            ++stats_.busy_ticks;
            skip = true;
        }
        if (!skip) {
            m.track = track_by_lba_(m.lba);

            const std::uint8_t rounds = m.cdda_first;
            for (std::uint8_t i = 0; i <= rounds; ++i) {

                if (toc.tracks[m.track].type == TrackType::Cdda) {
                    sec_buf_[0] = 0x30;
                    sec_buf_[1] = 0x09;
                    if (m.lba >= 0) {
                        const Lba at{static_cast<std::uint32_t>(m.lba)};
                        if (ports_.disc.take(
                                svc::DiscForm::RawFrame,
                                at,
                                std::as_writable_bytes(std::span(sec_buf_ + 2, kRawFrame))
                            )
                            == 0) {
                            ++stats_.substitutes;
                        }
                        ports_.disc.hint(svc::DiscForm::RawFrame, Lba{at.v + 1});
                    }
                    if (auto r = send_frame(
                            ports_.tx,
                            ports_.profile.io.cdda,
                            std::span(sec_buf_, kRawFrame + 2)
                        );
                        !r) {
                        return r;
                    }
                    ++stats_.cdda_sectors;
                    build_subcode_frame_(m.lba);
                    if (auto r = send_frame(
                            ports_.tx,
                            ports_.profile.io.subcode,
                            std::span(sub_buf_, 100)
                        );
                        !r) {
                        return r;
                    }
                }
                ++m.lba;
            }
            m.cdda_first = 0;

            const bool past_end = m.cdda.past_end(m.lba);
            const bool data_hit = toc.tracks[m.track].type != TrackType::Cdda;
            if (past_end || data_hit || m.track > toc.last) {
                if (m.cdda_mode == kPceCddaModeLoop) {
                    m.lba = m.cdda.start();
                    hint_at_(m.lba);
                } else {
                    m.state = kPceStateIdle;
                }
                if (m.int_pend) {
                    if (auto r = send_status_(kPceStatusGood); !r)
                        return r;
                }
                m.int_pend = false;
            }
        }
        if (m.subq_pend) {
            m.subq_pend = false;
            subq_send = true;
        }
    } else if (m.state == kPceStatePause) {
        subq_send = m.subq_pend;
        m.subq_pend = false;

        build_subcode_frame_(m.lba);
        if (auto r = send_frame(ports_.tx, ports_.profile.io.subcode, std::span(sub_buf_, 100));
            !r) {
            return r;
        }
        if (m.latency > 0)
            --m.latency;
    } else {
        subq_send = m.subq_pend;
        m.subq_pend = false;
    }

    if (subq_send) {

        if (auto r = send_frame(ports_.tx, ports_.profile.io.data, std::span(m.subq, 12)); !r) {
            return r;
        }
        return send_status_(kPceStatusGood);
    }
    return {};
}

void PceScsiEngine::do_reset_machine() noexcept {

    auto& m = st_;
    m.latency = 0;
    m.audio_delay = 0;
    m.track = 0;
    m.lba = 0;
    m.is_data = true;
    m.state = m.loaded ? kPceStateIdle : kPceStateNoDisc;
    m.has_status = false;
    m.data_req = false;
    m.can_read_next = false;
    m.cdda.clear();
    m.cdda_mode = kPceCddaModeSilent;
    m.int_pend = false;
    m.subq_pend = false;
    m.stat = 0;
}

Ex<void> PceScsiEngine::command_exec_() {

    auto& m = st_;
    const svc::Toc& toc = ports_.geom.toc;
    m.int_pend = false;

    const auto decode_target = [&](bool clamp_track) -> std::int32_t {
        switch (m.comm[9] & 0xC0u) {
            case 0x40: {

                const std::uint32_t mm = u8_from_bcd(m.comm[2]);
                const std::uint32_t ss = u8_from_bcd(m.comm[3]);
                const std::uint32_t ff = u8_from_bcd(m.comm[4]);
                return static_cast<std::int32_t>(ff + ss * 75u + mm * 60u * 75u) - 150;
            }
            case 0x80: {
                std::uint32_t track = u8_from_bcd(m.comm[2]);
                if (track == 0)
                    track = 1;
                if (clamp_track) {

                    const std::uint32_t last = toc.last ? toc.last : 1u;
                    if (track > last)
                        track = last;
                    return static_cast<std::int32_t>(toc.tracks[track - 1].start.v);
                }
                if (toc.last == 0 || track - 1 >= toc.last) {
                    return static_cast<std::int32_t>(toc.end.v);
                }
                return static_cast<std::int32_t>(toc.tracks[track - 1].start.v);
            }
            default:
                return static_cast<std::int32_t>(
                    (static_cast<std::uint32_t>(m.comm[3]) << 16)
                    | (static_cast<std::uint32_t>(m.comm[4]) << 8)
                    | m.comm[5]
                );
        }
    };

    switch (m.comm[0]) {
        case kPceCommTestUnit:
            if (m.state == kPceStateNoDisc) {
                m.sense.raise(kSenseNotReady, kNseNoDisc);
                return send_status_(kPceStatusCheckCond);
            }
            return send_status_(kPceStatusGood);

        case kPceCommRequestSense: {

            std::uint8_t buf[20] = {};
            buf[0] = 18;
            buf[1] = 0x80;
            buf[2] = 0x70;
            const ScsiSense::Block sense = m.sense.take();
            buf[4] = sense.key;
            buf[9] = 0x0A;
            buf[14] = sense.asc;
            buf[15] = sense.ascq;
            buf[16] = sense.fru;
            if (auto r = send_frame(ports_.tx, ports_.profile.io.data, std::span(buf, 20)); !r) {
                return r;
            }
            return send_status_(kPceStatusGood);
        }

        case kPceCommGetDirInfo: {

            std::uint8_t buf[6] = {};
            buf[0] = 4;
            buf[1] = 0x80;
            switch (m.comm[1]) {
                case 1: {
                    const Msf msf = lba_to_msf(static_cast<std::int32_t>(toc.end.v) + 150);
                    buf[2] = bcd(msf.m);
                    buf[3] = bcd(msf.s);
                    buf[4] = bcd(msf.f);
                    buf[5] = 0;
                    break;
                }
                case 2: {

                    std::uint32_t track = u8_from_bcd(m.comm[2]);
                    const std::uint32_t last = toc.last ? toc.last : 1u;
                    if (track < 1)
                        track = 1;
                    if (track > last)
                        track = last;
                    const auto& tk = toc.tracks[track - 1];
                    const Msf msf = lba_to_msf(static_cast<std::int32_t>(tk.start.v) + 150);
                    buf[2] = bcd(msf.m);
                    buf[3] = bcd(msf.s);
                    buf[4] = bcd(msf.f);
                    buf[5] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(tk.type) << 2);
                    break;
                }
                case 0:
                default:
                    buf[2] = 1;
                    buf[3] = bcd(toc.last);
                    buf[4] = 0;
                    buf[5] = 0;
                    break;
            }
            if (auto r = send_frame(ports_.tx, ports_.profile.io.data, std::span(buf, 6)); !r) {
                return r;
            }
            return send_status_(kPceStatusGood);
        }

        case kPceCommRead6: {
            const std::int32_t new_lba = static_cast<std::int32_t>(
                ((static_cast<std::uint32_t>(m.comm[1]) << 16)
                 | (static_cast<std::uint32_t>(m.comm[2]) << 8)
                 | m.comm[3])
                & 0x1FFFFFu
            );
            const std::uint32_t cnt = m.comm[4] ? m.comm[4] : 256u;
            m.track = track_by_lba_(new_lba);

            bool matched = false;
            for (const auto& fp : ports_.profile.seek_fast_paths) {
                if (m.lba == new_lba && cnt == fp.count) {
                    m.latency = fp.latency_ticks;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                if (m.comm[13] & 0x80u) {
                    m.latency = 0;
                } else {
                    m.latency = seek_ticks_(m.lba, new_lba);
                    m.audio_delay = 0;
                }
            }
            m.lba = new_lba;
            hint_at_(m.lba);
            m.cnt = cnt;
            m.can_read_next = true;
            m.state = kPceStateRead;
            break;
        }

        case kPceCommModeSelect6:
            if (m.comm[4]) {
                m.data_req = true;
            } else {
                return send_status_(kPceStatusGood);
            }
            break;

        case kPceCommSapsp: {
            const std::int32_t new_lba = decode_target(true);
            if (m.comm[13] & 0x80u) {
                m.latency = 0;
                m.audio_delay = 0;
            } else {

                const std::uint32_t seek = seek_ticks_(m.lba, new_lba);
                m.audio_delay = static_cast<std::uint32_t>(220.0f / 13.33f);
                if (seek > m.audio_delay) {
                    m.latency = seek - m.audio_delay;
                } else {
                    m.latency = seek;
                    m.audio_delay = 0;
                }
            }
            m.lba = new_lba;
            hint_at_(m.lba, true);
            m.track = track_by_lba_(new_lba);
            m.cdda.play(new_lba, static_cast<std::int32_t>(toc.end.v));
            m.cdda_mode = m.comm[1];
            m.cdda_first = 1;
            m.state = (m.cdda_mode == kPceCddaModeSilent) ? kPceStatePause : kPceStatePlay;
            m.stat = kPceStatusGood;
            m.has_status = true;
            break;
        }

        case kPceCommSapep: {
            const std::int32_t new_lba = decode_target(false);
            m.cdda_mode = m.comm[1];
            m.cdda.set_end(new_lba);
            m.state = (m.cdda_mode == kPceCddaModeSilent) ? kPceStateIdle : kPceStatePlay;
            if (m.cdda_mode == kPceCddaModeInterrupt) {
                m.int_pend = true;
            } else {
                return send_status_(kPceStatusGood);
            }
            break;
        }

        case kPceCommPause:
            m.state = kPceStatePause;
            return send_status_(kPceStatusGood);

        case kPceCommReadSubq:
            m.subq_pend = true;
            break;

        default:
            m.sense.raise(kSenseIllegalRequest, kNseInvalidCommand);
            m.has_status = false;
            return send_status_(kPceStatusCheckCond);
    }
    return {};
}

}
