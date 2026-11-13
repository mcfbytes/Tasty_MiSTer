// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/pce_scsi_engine.h"

#include "hal/disk_lamp.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

#include "cd_core_detail.h"
#include "cores/cd_engine.h"
#include "cores/cd_head_prime.h"
#include "cores/cd_msf.h"
#include "cores/cdwire.h"
#include "cores/pce_command.h"
#include "cores/pce_dir_info_reply.h"
#include "cores/pce_sense_reply.h"
#include "cores/pce_subq.h"
#include "cores/pce_subq_reply.h"
#include "cores/subcode_q_frame.h"
#include "infra/endian.h"
#include "infra/error.h"
#include "proto/spi_fio_queue.h"
#include "proto/types.h"
#include "svc/toc.h"

namespace mister::cores {

namespace {

constexpr std::size_t kPceCddaFrame = svc::kRawFrameBytes + 2;
constexpr std::size_t kPceSubFrame = svc::kSubcodeBytes + 4;
constexpr std::size_t kStatusCommandWords = 1 + CdEngine::kMaxStatusWords;
static_assert(
    2 * (kPceCddaFrame + kPceSubFrame) + 12 + 2 * 2 * kStatusCommandWords
        <= proto::SpiFioQueue::kSlabBytes,
    "the doubled CD-DA pass and its follow-ups fit the slab"
);
static_assert(kStatusCommandWords <= proto::SpiFioQueue::kMaxCommandWords);

constexpr PollTicks::TickLength kPceTick{13.33F};
constexpr PollTicks kLoadSettleTicks{10};
constexpr PollTicks kSpinUpTicks = PollTicks::spanning(svc::SeekTime{220}, kPceTick);
static_assert(kSpinUpTicks == PollTicks{16});
static_assert(std::is_same_v<PollTicks::Span, svc::SeekTime>);

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

}

void PceScsiEngine::do_notify_disc(bool loaded, const svc::DiscGeometry&) noexcept {

    st_.loaded = loaded;
    st_.state = loaded ? State::Idle : State::NoDisc;
    if (loaded)
        st_.latency = kLoadSettleTicks;
}

bool PceScsiEngine::do_streaming(const svc::DiscGeometry&) const noexcept {

    return !st_.latency.owed() && st_.state == State::Read;
}

CdDriveState PceScsiEngine::drive_state_() const noexcept {
    switch (st_.state) {
        case State::NoDisc:
            return CdDriveState::NoDisc;
        case State::Idle:
            return CdDriveState::Stopped;
        case State::Read:
            return CdDriveState::Reading;
        case State::Play:
            return CdDriveState::Playing;
        case State::Pause:
            return CdDriveState::Paused;
    }
    return CdDriveState::Stopped;
}

Ex<void> PceScsiEngine::do_push_sector(CdDialectPorts& p) {
    if (st_.state != State::Read || st_.latency.owed() || !p.geom.toc.last.contains(st_.track)) {
        return {};
    }
    return read_send_(p);
}

Ex<void> PceScsiEngine::do_accept(std::span<const std::uint16_t> w, CdDialectPorts& p) {

    switch (w[6] & 0xFFu) {
        case 0: {

            std::array<std::uint8_t, PceCommand::kBytes> raw{};
            for (std::size_t i = 0; i < PceCommand::kWords; ++i) {
                infra::store_le<std::uint16_t>(
                    std::span<std::uint8_t, sizeof(std::uint16_t)>(
                        raw.data() + i + i,
                        sizeof(std::uint16_t)
                    ),
                    w[i]
                );
            }
            return command_exec_(PceCommand::of(raw), p);
        }
        case 1:

            return send_status_(Status::Good, p.link);
        case 2:
            st_.can_read_next = true;
            break;
        default:
            request_reset();
            break;
    }
    return {};
}

Ex<void> PceScsiEngine::send_status_(Status status, ICdLink& link) {
    return send_status_words_(cdwire::pce_set_words(word_of(status), st_.region, false), link);
}

Ex<void> PceScsiEngine::send_data_request_(ICdLink& link) {
    return send_status_words_(cdwire::pce_set_words(word_of(Status::Good), st_.region, true), link);
}

svc::TrackIndex PceScsiEngine::track_by_lba_(HeadLba lba, const svc::DiscGeometry& geom) const {
    return track_scan(geom.toc, lba);
}

PollTicks PceScsiEngine::seek_ticks_(HeadMove move, const svc::DiscGeometry& geom) const {

    const std::optional<Lba> a = move.from.readable();
    const std::optional<Lba> b = move.to.readable();
    if (!a || !b)
        return PollTicks{};
    return PollTicks::spanning(geom.seek_time(*a, *b), kPceTick);
}

Ex<bool> PceScsiEngine::drain_pending(ICdLink& link) {

    auto& m = st_;
    if (m.has_status && !m.latency.owed()) {
        if (auto r = send_status_(Status::Good, link); !r)
            return std::unexpected(r.error());
        m.has_status = false;
        return true;
    }
    if (m.data_req && !m.latency.owed()) {
        if (auto r = send_data_request_(link); !r)
            return std::unexpected(r.error());
        m.data_req = false;
        return true;
    }
    return false;
}

Ex<void> PceScsiEngine::do_tick(CdDialectPorts& p) {

    if (auto r = drain_pending(p.link); !r)
        return std::unexpected(r.error());
    return update_(p);
}

void PceScsiEngine::hint_at_(HeadLba lba, bool doubled, const svc::DiscGeometry& geom) noexcept {
    const std::optional<Lba> at = lba.readable();
    if (!at)
        return;
    const svc::Toc& toc = geom.toc;
    const svc::TrackIndex tr = track_by_lba_(lba, geom);
    const bool data = toc.last.contains(tr) && toc.at(tr).type != TrackType::Cdda;

    prime_head(
        facts_.disc,
        *at,
        {.form = data ? svc::DiscForm::UserData : svc::DiscForm::RawFrame,
         .pair = doubled && !data,
         .subcode = true}
    );
}

void PceScsiEngine::build_subcode_frame_(HeadLba lba, const svc::DiscGeometry& geom) {

    auto& m = st_;
    if (lba != m.subc_lba && !m.latency.owed()) {
        bool have = false;
        const std::optional<Lba> at = lba.readable();
        if (at) {
            have = facts_.disc
                       .take(
                           svc::DiscAsk{.form = svc::DiscForm::Subcode, .lba = *at},
                           std::as_writable_bytes(std::span(m.subc, svc::kSubcodeBytes))
                       )
                       .is_bytes();
            if (const std::optional<Lba> next = (lba + SectorCount{1}).readable()) {
                facts_.disc.hint(svc::DiscAsk{.form = svc::DiscForm::Subcode, .lba = *next});
            }
        }
        if (!have) {
            if (at)
                ++stats_.subcode_substitutes;

            PceSubq q;
            q.set_track(m.track.number());
            const svc::Toc& toc = geom.toc;

            const HeadLba base = head_of(toc.at(m.track).start);

            q.set_relative(stock_msf_of(m.lba - base));
            q.set_absolute(msf_of(m.lba + kPregapSectors));
            q.seal();
            const auto block = SubcodeQFrame{}.wire_block(q.bytes());
            for (std::size_t i = 0; i < svc::kSubcodeBytes; ++i)
                m.subc[i] = block[i];
        }
        m.subc_lba = lba;
    }
    sub_buf_[0] = 0x62;
    sub_buf_[1] = 0x00;
    sub_buf_[2] = 0x00;
    sub_buf_[3] = 0x80;
    for (std::size_t i = 0; i < svc::kSubcodeBytes; ++i)
        sub_buf_[4 + i] = m.subc[i];
}

Ex<void> PceScsiEngine::read_send_(CdDialectPorts& p) {

    auto& m = st_;
    const svc::Toc& toc = p.geom.toc;
    if (!m.can_read_next)
        return {};

    if (p.egress_busy()) {
        ++stats_.busy_ticks;
        return {};
    }
    m.can_read_next = false;
    if (p.lamp)
        p.lamp->note();

    const svc::Track& tk = toc.at(m.track);
    if (tk.type != TrackType::Cdda) {
        sec_buf_[0] = 0x00;
        sec_buf_[1] = 0x08 | 0x80;
        if (const std::optional<Lba> at = m.lba.readable()) {
            const svc::DiscTake got = facts_.disc.take(
                svc::DiscAsk{.form = svc::DiscForm::UserData, .lba = *at},
                std::as_writable_bytes(std::span(sec_buf_ + 2, svc::kUserDataBytes))
            );
            if (!got.is_bytes()) {
                ++stats_.substitutes;
            }
            if (const std::optional<Lba> next = (m.lba + SectorCount{1}).readable()) {
                facts_.disc.hint(svc::DiscAsk{.form = svc::DiscForm::UserData, .lba = *next});
            }
        }
        if (auto r = send_frame(p.link, io_.data, std::span(sec_buf_, svc::kUserDataBytes + 2));
            !r) {
            return r;
        }
        ++stats_.data_sectors;
    } else if (m.lba >= head_of(tk.start)) {
        m.is_data = false;
    }

    m.cnt -= SectorCount{1};
    if (m.cnt == SectorCount{}) {
        m.has_status = true;
        m.state = State::Idle;
    }
    m.lba += SectorCount{1};
    if (toc.last.contains(m.track) && m.lba >= head_of(toc.at(m.track).end)) {
        m.track += svc::TrackCount{1};
        m.is_data = true;
    }
    return {};
}

Ex<void> PceScsiEngine::deliver_cdda_pass_(CdDialectPorts& p) {
    auto& m = st_;
    const svc::Toc& toc = p.geom.toc;

    if (toc.at(m.track).type == TrackType::Cdda) {
        sec_buf_[0] = 0x30;
        sec_buf_[1] = 0x09;
        if (const std::optional<Lba> at = m.lba.readable()) {
            const svc::DiscTake got = facts_.disc.take(
                svc::DiscAsk{.form = svc::DiscForm::RawFrame, .lba = *at},
                std::as_writable_bytes(std::span(sec_buf_ + 2, svc::kRawFrameBytes))
            );
            if (!got.is_bytes()) {
                ++stats_.substitutes;
            }
            if (const std::optional<Lba> next = (m.lba + SectorCount{1}).readable()) {
                facts_.disc.hint(svc::DiscAsk{.form = svc::DiscForm::RawFrame, .lba = *next});
            }
        }
        if (auto r = send_frame(p.link, io_.cdda, std::span(sec_buf_, svc::kRawFrameBytes + 2));
            !r) {
            return r;
        }
        ++stats_.cdda_sectors;
        build_subcode_frame_(m.lba, p.geom);
        if (auto r = send_frame(p.link, io_.subcode, std::span(sub_buf_, 100)); !r) {
            return r;
        }
    }
    m.lba += SectorCount{1};
    return {};
}

void PceScsiEngine::pump_pipeline_(const svc::DiscGeometry& geom) noexcept {
    auto& m = st_;
    const svc::Toc& toc = geom.toc;
    const std::optional<Lba> head = m.lba.readable();
    if (toc.last == svc::TrackCount{} || !head)
        return;
    const svc::TrackIndex tr = track_by_lba_(m.lba, geom);
    const bool data = toc.last.contains(tr) && toc.at(tr).type != TrackType::Cdda;

    prime_head(
        facts_.disc,
        *head,
        {.form = data ? svc::DiscForm::UserData : svc::DiscForm::RawFrame, .subcode = true}
    );
}

Ex<void> PceScsiEngine::update_(CdDialectPorts& p) {
    auto& m = st_;
    const svc::Toc& toc = p.geom.toc;

    pump_pipeline_(p.geom);

    PceSubqReply subq;
    subq.set_play_state(
        m.state == State::Pause ? PceSubqReply::PlayState::Paused
                                : (m.state == State::Play ? PceSubqReply::PlayState::Playing
                                                          : PceSubqReply::PlayState::Stopped)
    );
    subq.set_track(m.track.number());
    {

        const HeadLba base = head_of(toc.at(m.track).start);

        subq.set_relative(stock_msf_of(m.lba - base));
        subq.set_absolute(msf_of(m.lba + kPregapSectors));
    }

    bool subq_send = false;

    if (m.state == State::Read) {

        if (m.latency.owed()) {
            m.latency.spend();
            return {};
        }
        if (!toc.last.contains(m.track)) {
            m.state = State::Idle;
            return {};
        }
        return read_send_(p);
    }
    if (m.state == State::Play) {
        bool skip = false;
        if (m.latency.owed()) {
            m.latency.spend();
            skip = true;
        } else if (m.audio_delay.owed()) {
            m.audio_delay.spend();
            skip = true;
        } else if (p.egress_busy()) {
            ++stats_.busy_ticks;
            skip = true;
        }
        if (!skip) {
            m.track = track_by_lba_(m.lba, p.geom);
            if (p.lamp)
                p.lamp->note();

            if (auto r = deliver_cdda_pass_(p); !r)
                return r;
            if (st_.cdda_first) {
                if (auto r = deliver_cdda_pass_(p); !r)
                    return r;
            }
            st_.cdda_first = false;

            const bool past_end = m.cdda.past_end(m.lba);
            const bool data_hit = toc.at(m.track).type != TrackType::Cdda;
            if (past_end || data_hit || m.track > toc.last.next_index()) {
                if (m.cdda_mode == PceCommand::CddaMode::Loop) {
                    m.lba = m.cdda.start();
                    hint_at_(m.lba, false, p.geom);
                } else {
                    m.state = State::Idle;
                }
                if (m.int_pend) {
                    if (auto r = send_status_(Status::Good, p.link); !r)
                        return r;
                }
                m.int_pend = false;
            }
        }
        if (m.subq_pend) {
            m.subq_pend = false;
            subq_send = true;
        }
    } else if (m.state == State::Pause) {
        subq_send = m.subq_pend;
        m.subq_pend = false;

        build_subcode_frame_(m.lba, p.geom);
        if (auto r = send_frame(p.link, io_.subcode, std::span(sub_buf_, 100)); !r) {
            return r;
        }
        if (m.latency.owed())
            m.latency.spend();
    } else {
        subq_send = m.subq_pend;
        m.subq_pend = false;
    }

    if (subq_send) {

        if (auto r = send_frame(p.link, io_.data, subq.bytes()); !r) {
            return r;
        }
        return send_status_(Status::Good, p.link);
    }
    return {};
}

void PceScsiEngine::do_reset_machine() noexcept {

    auto& m = st_;
    m.latency = PollTicks{};
    m.audio_delay = PollTicks{};
    m.track = svc::TrackIndex{};
    m.lba = HeadLba{};
    m.is_data = true;
    m.state = m.loaded ? State::Idle : State::NoDisc;
    m.has_status = false;
    m.data_req = false;
    m.can_read_next = false;
    m.cdda.clear();
    m.cdda_mode = PceCommand::CddaMode::Silent;
    m.int_pend = false;
    m.subq_pend = false;
}

Ex<void> PceScsiEngine::command_exec_(const PceCommand& command, CdDialectPorts& p) {

    auto& m = st_;
    const svc::Toc& toc = p.geom.toc;
    m.int_pend = false;

    const std::optional<PceCommand::Opcode> op = command.opcode();
    if (!op) {
        m.sense.raise(SenseKey::IllegalRequest, Asc::InvalidCommand);
        m.has_status = false;
        return send_status_(Status::CheckCondition, p.link);
    }

    switch (*op) {
        case PceCommand::TestUnit:
            if (m.state == State::NoDisc) {
                m.sense.raise(SenseKey::NotReady, Asc::NoDisc);
                return send_status_(Status::CheckCondition, p.link);
            }
            return send_status_(Status::Good, p.link);

        case PceCommand::RequestSense: {

            const ScsiSense::Block sense = m.sense.take();
            PceSenseReply reply;
            reply.set_key(sense_key_byte(sense.key));
            reply.set_asc(asc_byte(sense.asc));
            if (auto r = send_frame(p.link, io_.data, reply.bytes()); !r) {
                return r;
            }
            return send_status_(Status::Good, p.link);
        }

        case PceCommand::GetDirInfo: {

            PceDirInfoReply reply;
            switch (command.dir_info().value_or(PceCommand::DirInfo::Summary)) {
                case PceCommand::DirInfo::DiscEnd:
                    reply.set_disc_end(msf_of(head_of(toc.end) + kPregapSectors));
                    break;
                case PceCommand::DirInfo::Track: {

                    const svc::Track& tk = toc.at(command.clamped_track(toc).index());
                    reply.set_track(msf_of(head_of(tk.start) + kPregapSectors), tk.type);
                    break;
                }
                case PceCommand::DirInfo::Summary:
                    reply.set_summary(toc.last);
                    break;
            }
            if (auto r = send_frame(p.link, io_.data, reply.bytes()); !r) {
                return r;
            }
            return send_status_(Status::Good, p.link);
        }

        case PceCommand::Read6: {
            const PceCommand::ReadOperands operands = command.read6();
            m.track = track_by_lba_(operands.head, p.geom);

            bool matched = false;
            for (const auto& fp : row_.seek_fast_paths) {
                if (m.lba == operands.head && operands.count == fp.count) {
                    m.latency = fp.latency_ticks;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                if (command.fast_seek()) {
                    m.latency = PollTicks{};
                } else {
                    m.latency = seek_ticks_(HeadMove{m.lba, operands.head}, p.geom);
                    m.audio_delay = PollTicks{};
                }
            }
            m.lba = operands.head;
            hint_at_(m.lba, false, p.geom);
            m.cnt = operands.count;
            m.can_read_next = true;
            m.state = State::Read;
            break;
        }

        case PceCommand::ModeSelect6:
            if (command.requests_data()) {
                m.data_req = true;
            } else {
                return send_status_(Status::Good, p.link);
            }
            break;

        case PceCommand::Sapsp: {
            const HeadLba new_lba = command.target(true, toc);
            if (command.fast_seek()) {
                m.latency = PollTicks{};
                m.audio_delay = PollTicks{};
            } else {

                const PollTicks seek = seek_ticks_(HeadMove{m.lba, new_lba}, p.geom);
                m.audio_delay = kSpinUpTicks;
                if (seek > m.audio_delay) {
                    m.latency = seek - m.audio_delay;
                } else {
                    m.latency = seek;
                    m.audio_delay = PollTicks{};
                }
            }
            m.lba = new_lba;
            hint_at_(m.lba, true, p.geom);
            m.track = track_by_lba_(new_lba, p.geom);
            m.cdda.play(new_lba, head_of(toc.end));
            m.cdda_mode = command.cdda_mode();
            m.cdda_first = true;
            m.state = (m.cdda_mode == PceCommand::CddaMode::Silent) ? State::Pause : State::Play;
            m.has_status = true;
            break;
        }

        case PceCommand::Sapep: {
            const HeadLba new_lba = command.target(false, toc);
            m.cdda_mode = command.cdda_mode();
            m.cdda.set_end(new_lba);
            m.state = (m.cdda_mode == PceCommand::CddaMode::Silent) ? State::Idle : State::Play;
            if (m.cdda_mode == PceCommand::CddaMode::Interrupt) {
                m.int_pend = true;
            } else {
                return send_status_(Status::Good, p.link);
            }
            break;
        }

        case PceCommand::Pause:
            m.state = State::Pause;
            return send_status_(Status::Good, p.link);

        case PceCommand::ReadSubq:
            m.subq_pend = true;
            break;
    }
    return {};
}

}
