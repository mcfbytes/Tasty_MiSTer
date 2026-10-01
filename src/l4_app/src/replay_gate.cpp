// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/replay_gate.h"

#include <algorithm>

#include "infra/message_sum.h"
#include "proto/spi_fio_queue.h"
#include "reactor/tick.h"
#include "svc/input_emitter.h"

namespace mister::app {

static_assert(ReplayGate::kMaxRoundWords <= proto::tightest_round_budget_words(),
              "a replay round's own words never exceed the round budget they are taken from");

std::int64_t ReplayGate::now_ns_() const noexcept { return w_.clock->now().count(); }

Ex<proto::FrameCount> ReplayGate::read_counter_() noexcept {
    if (w_.frames == nullptr) return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    round_words_ += proto::FrameCounterRead::kReadWords;
    return w_.frames->read();
}

void ReplayGate::sample_control_() noexcept {
    if (w_.control == nullptr) return;
    ReplayControl c{};

    if (control_seen_.take_if_changed(*w_.control, c)) ctrl_ = c;
}

void ReplayGate::tick(bool ready, std::uint32_t core_edge_seq, proto::LateAnswers late) noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    sample_control_();
    blk_seen_ = late.count;
    if (level_ == ReplayLevel::Running &&
        (late.count - blk_base_ != status_.blk_late || late.max_us > status_.blk_late_max_us)) {
        status_.blk_late = late.count - blk_base_;
        status_.blk_late_max_us = std::max(status_.blk_late_max_us, late.max_us);
        dirty_ = true;
    }

    if (release_ports_ != 0 && (core_edge_seq != edge_seq_ || ready)) {
        if (core_edge_seq == edge_seq_) force_release_(release_ports_);
        release_ports_ = 0;
        dirty_ = true;
    }
    if (level_ == ReplayLevel::Idle) {
        edge_seq_ = core_edge_seq;
        return;
    }
    ready_ = ready;

    if (core_edge_seq != edge_seq_) return disarm_(ReplayEnd::CoreSwitch, false);
    if (ctrl_.gen != gen_) return disarm_(ReplayEnd::Superseded, true);
    if (ctrl_.op != ReplayOp::Play) return disarm_(ReplayEnd::Stopped, true);
    now_ = now_ns_();

    if (level_ == ReplayLevel::AwaitPowerOn) return;
    if (!ready) {
        ++status_.held_rounds;
        return;
    }
    if (!sample_ref_()) return;
    if (!epoch_known_) {
        if (p0_ == ReplayMsg::P0Parity::AfterSilence && now_ - end_ns_ > kSilenceWindowNs)
            disarm_(ReplayEnd::EpochAmbiguous, true);
        return;
    }
    mf_ = static_cast<std::int64_t>(frames_) - epoch_ - lead_;
    if (held_ && due_(*held_)) {
        const ReplayMsg::Input r = *held_;
        held_.reset();
        apply_(r);
    }
}

bool ReplayGate::sample_ref_() noexcept {

    if (now_ - last_sample_ns_ > alias_ns_) {
        disarm_(ReplayEnd::RefLost, true);
        return false;
    }
    std::uint32_t raw = 0;
    if (status_.ref == ReplayRef::Counter) {
        const auto r = read_counter_();
        if (!r || !r->supported) {
            ++status_.link_errors;
            return false;
        }
        raw = r->count;
    } else {
        reactor::FrameRecord rec{};
        if (w_.vsync == nullptr || w_.vsync->sample_into(rec) == 0) return false;
        raw = rec.seq;
    }
    if (raw == last_raw_) return true;
    last_sample_ns_ = now_;
    const std::uint32_t d = (raw - last_raw_) & wrap_mask_;
    last_raw_ = raw;
    frames_ += d;
    edge_ns_ = now_;
    if (d > 1) status_.skipped_edges += d - 1;
    if (!epoch_known_) {
        if (p0_ == ReplayMsg::P0Parity::AfterSilence) {
            decide_silence_(d);
        } else {
            decide_epoch_(d);
        }
    }
    return level_ == ReplayLevel::Running;
}

namespace {

std::int64_t ceil_div(std::int64_t a, std::int64_t b) {
    return a >= 0 ? (a + b - 1) / b : -(-a / b);
}

std::int64_t released(std::int64_t j, ReplayMsg::P0Parity p0, std::uint32_t first_count) {
    if (p0 == ReplayMsg::P0Parity::Any || j < 0) return j;
    const std::uint32_t want = p0 == ReplayMsg::P0Parity::Odd ? 1u : 0u;
    return ((first_count + static_cast<std::uint32_t>(j)) & 1u) == want ? j : j + 1;
}

struct EpochPick {
    std::int64_t j;
    std::int64_t hi;
};

EpochPick epoch_at(std::int64_t delay, std::int64_t poweron_ns, std::int64_t period_ns,
                   std::int64_t line0_ns, ReplayMsg::P0Parity p0, std::uint32_t first_count) {
    const std::int64_t x = poweron_ns + period_ns - line0_ns - delay;
    return {released(ceil_div(x - ReplayGate::kEndSlackNs, period_ns), p0, first_count),
            released(ceil_div(x + reactor::kTickNs, period_ns), p0, first_count)};
}

}  // namespace

void ReplayGate::decide_epoch_(std::uint32_t delta) noexcept {
    const std::int64_t d = edge_ns_ - end_ns_;
    status_.epoch_delay_us = static_cast<std::uint32_t>(std::max<std::int64_t>(0, d) / 1000);
    const EpochPick p = epoch_at(d, poweron_ns_, period_ns_, line0_ns_, p0_, last_raw_);
    const bool band = p.j != p.hi;

    const bool untimed = p.j == 0 && -static_cast<std::int64_t>(lead_) >= kFloorBack;

    bool slipped = false;
    std::uint8_t slip = 0;
    if ((untimed || p.j < 0) && p0_ == ReplayMsg::P0Parity::Any && late_frames_ != 0 &&
        period_ns_ > 0) {
        for (std::uint8_t k = late_frames_; k != 0; --k) {
            const std::int64_t earlier = d - static_cast<std::int64_t>(k) * period_ns_;
            const EpochPick s =
                epoch_at(earlier, poweron_ns_, period_ns_, line0_ns_, p0_, last_raw_);
            if (s.j == s.hi && s.j == static_cast<std::int64_t>(k)) {
                slipped = true;
                slip = k;
                break;
            }
        }
    }
    if (!slipped && (delta != 1 || p.j < 0 || untimed || band)) {

        status_.epoch_fail = delta != 1 ? EpochFail::Retry
                             : untimed  ? EpochFail::Untimed
                             : p.j < 0  ? EpochFail::Before
                                        : EpochFail::Retry;
        return disarm_(ReplayEnd::EpochAmbiguous, true);
    }

    epoch_ = slipped ? slip : static_cast<std::uint32_t>(1 + p.j);
    status_.epoch_edge = epoch_;
    epoch_known_ = true;
}

void ReplayGate::decide_silence_(std::uint32_t delta) noexcept {
    const std::int64_t gap = edge_ns_ - prev_edge_ns_;
    prev_edge_ns_ = edge_ns_;
    if (gap <= period_ns_ + kSilenceSlackNs) return;

    if (delta != 1) return disarm_(ReplayEnd::EpochAmbiguous, true);
    status_.epoch_delay_us =
        static_cast<std::uint32_t>(std::max<std::int64_t>(0, edge_ns_ - end_ns_) / 1000);
    epoch_ = frames_;
    status_.epoch_edge = epoch_;
    epoch_known_ = true;
}

bool ReplayGate::pre_epoch_(std::uint32_t frame) const noexcept {
    return static_cast<std::int64_t>(frame) + lead_ < -static_cast<std::int64_t>(floor_back_);
}

bool ReplayGate::live_() const noexcept { return epoch_known_ && frames_ + floor_back_ >= epoch_; }

bool ReplayGate::due_(const ReplayMsg::Input& r) const noexcept {

    if (!ready_ || level_ != ReplayLevel::Running || !live_()) return false;
    const auto first = static_cast<std::int64_t>(r.first);
    return first < mf_ || (first == mf_ && now_ - edge_ns_ >= offset_ns_);
}

void ReplayGate::note_late_(std::uint32_t first) noexcept {
    ++status_.late;
    if (status_.first_late_frame < 0) status_.first_late_frame = static_cast<std::int32_t>(first);
}

void ReplayGate::apply_(const ReplayMsg::Input& r) noexcept {
    const auto first = static_cast<std::int64_t>(r.first);
    const auto last = static_cast<std::int64_t>(r.last);
    covered_ = std::max(covered_, last);
    if (pre_epoch_(r.first)) {
        ++status_.pre_epoch;
        write_(r);
        return;
    }
    if (last < mf_) return note_late_(r.first);
    if (first < mf_) {
        note_late_(r.first);
    } else {
        const auto us = static_cast<std::uint32_t>((now_ - edge_ns_) / 1000);
        status_.apply_min_us = status_.on_time == 0 ? us : std::min(status_.apply_min_us, us);
        status_.apply_max_us = std::max(status_.apply_max_us, us);
        ++status_.on_time;
    }
    write_(r);
}

void ReplayGate::write_(const ReplayMsg::Input& r) noexcept {
    write_masks_(r.mask, ports_);
    status_.applied_frame = static_cast<std::int32_t>(std::min<std::int64_t>(mf_, r.last));
}

void ReplayGate::write_masks_(const std::array<std::uint32_t, kReplayPorts>& mask,
                              std::uint8_t ports) noexcept {
    proto::JoystickPort& joy = w_.emitter->joysticks();
    for (std::uint8_t p = 0; p < kReplayPorts; ++p) {
        if ((ports & (1u << p)) == 0) continue;
        const auto out = joy.submit(*w_.link, joy.wire_port(proto::PlayerIndex{p}),
                                    proto::JoyMask{mask[p]}, proto::JoyMask{});
        if (!out || *out) round_words_ += proto::JoystickPort::kMaxDigitalPushWords;
        if (!out) {
            ++status_.link_errors;
        } else if (*out) {
            ++status_.writes;
        }
    }
}

void ReplayGate::force_release_(std::uint8_t ports) noexcept {
    proto::JoystickPort& joy = w_.emitter->joysticks();
    for (std::uint8_t p = 0; p < kReplayPorts; ++p) {
        if ((ports & (1u << p)) == 0) continue;
        const proto::PlayerIndex port = joy.wire_port(proto::PlayerIndex{p});
        auto out = joy.submit(*w_.link, port, proto::JoyMask{}, proto::JoyMask{});
        if (out && !*out) out = joy.release(*w_.link, port, proto::JoyMask{~0u});
        if (!out || *out) round_words_ += proto::JoystickPort::kMaxDigitalPushWords;
        if (!out) {
            ++status_.link_errors;
        } else if (*out) {
            ++status_.writes;
        }
    }
}

bool ReplayGate::wants_record() const noexcept {
    if (level_ == ReplayLevel::Idle) return true;
    return !held_ && !end_seen_;
}

void ReplayGate::on(const ReplayMsg::Arm& a, const ReplayMsg::Head& h) noexcept {
    TASTY_SEAT_BODY(ReplayGate);

    sample_control_();
    const bool wired = w_.link != nullptr && w_.emitter != nullptr && w_.clock != nullptr;

    if (level_ != ReplayLevel::Idle || release_ports_ != 0 || ctrl_.op != ReplayOp::Play ||
        ctrl_.gen != h.gen || !wired || a.ports == 0 || a.period_ns == 0) {
        ++status_.stale;
        status_.gen = h.gen;
        status_.end = ReplayEnd::Refused;
        dirty_ = true;
        return;
    }
    const std::uint32_t stale = status_.stale;
    status_ = ReplayStatus{};
    status_.stale = stale;
    status_.gen = h.gen;
    status_.level = ReplayLevel::AwaitPowerOn;
    level_ = ReplayLevel::AwaitPowerOn;
    gen_ = h.gen;
    ports_ = static_cast<std::uint8_t>(a.ports & ((1u << kReplayPorts) - 1u));
    rom_index_ = a.rom_index;
    vsync_ok_ = a.vsync_ok != 0;
    lead_ = a.lead;
    offset_ns_ = static_cast<std::int64_t>(a.offset_us) * 1000;
    period_ns_ = a.period_ns;
    line0_ns_ = a.line0_ns;
    poweron_ns_ = a.poweron_ns;
    late_frames_ = a.late_frames;
    p0_ = a.p0;
    event_ = a.event;

    floor_back_ = p0_ == ReplayMsg::P0Parity::AfterSilence ? 0u : kFloorBack;
    alias_ns_ = period_ns_ * kAliasFrames;
    epoch_known_ = false;
    epoch_ = 0;
    frames_ = 0;
    mf_ = -1;
    covered_ = -1;
    underrun_at_ = -1;
    held_.reset();
    end_seen_ = false;
    end_frame_ = 0;
    dirty_ = true;
}

void ReplayGate::on(const ReplayMsg::Input& r, const ReplayMsg::Head& h) noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    if (level_ == ReplayLevel::Idle || h.gen != gen_ || r.last < r.first) {
        ++status_.stale;
        dirty_ = true;
        return;
    }
    if (due_(r)) return apply_(r);
    held_ = r;
}

void ReplayGate::on(const ReplayMsg::End& e, const ReplayMsg::Head& h) noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    if (level_ == ReplayLevel::Idle || h.gen != gen_) {
        ++status_.stale;
        dirty_ = true;
        return;
    }
    end_seen_ = true;
    end_frame_ = e.frame;
}

void ReplayGate::misrouted(const ReplayMsg&) noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    ++status_.stale;
    dirty_ = true;
}

void ReplayGate::settle() noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    if (level_ == ReplayLevel::Running && live_() && mf_ >= 0) {
        if (end_seen_ && !held_ && mf_ >= static_cast<std::int64_t>(end_frame_)) {
            disarm_(ReplayEnd::Finished, true);
        } else if (!held_ && mf_ > covered_ && mf_ != underrun_at_) {

            underrun_at_ = mf_;
            ++status_.underruns;
            if (status_.first_underrun_frame < 0)
                status_.first_underrun_frame = static_cast<std::int32_t>(mf_);
        }
    }

    if (level_ == ReplayLevel::Idle && (!dirty_ || release_ports_ != 0)) return;
    status_.level = level_;
    status_.movie_frame = static_cast<std::int32_t>(mf_);
    if (w_.status != nullptr) w_.status->publish(status_);
    dirty_ = false;
}

void ReplayGate::note_download_end(std::uint8_t wire_index) noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    if (level_ != ReplayLevel::AwaitPowerOn || event_ != ReplayMsg::PowerOnEvent::LoadEnd) return;
    if (rom_index_ != 0xFF && (wire_index & 0x3Fu) != rom_index_) return;
    begin_run_();
}

void ReplayGate::note_core_reset() noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    if (level_ != ReplayLevel::AwaitPowerOn || event_ != ReplayMsg::PowerOnEvent::ResetPulse)
        return;
    begin_run_();
}

void ReplayGate::begin_run_() noexcept {
    now_ = now_ns_();
    end_ns_ = now_;
    const auto c = read_counter_();
    if (c && c->supported) {
        status_.ref = ReplayRef::Counter;
        last_raw_ = c->count;
        wrap_mask_ = proto::FrameCounterRead::kWrapMask;
    } else {
        reactor::FrameRecord rec{};

        if (!vsync_ok_ || p0_ != ReplayMsg::P0Parity::Any || w_.vsync == nullptr ||
            w_.vsync->sample_into(rec) == 0) {
            return disarm_(ReplayEnd::NoReference, false);
        }
        status_.ref = ReplayRef::Vsync;
        last_raw_ = rec.seq;
        wrap_mask_ = 0xFFFF'FFFFu;
        alias_ns_ = period_ns_ * kAliasFrames;
    }
    frames_ = 0;
    edge_ns_ = now_;
    prev_edge_ns_ = now_;
    last_sample_ns_ = now_;
    blk_base_ = blk_seen_;
    level_ = ReplayLevel::Running;
    dirty_ = true;
}

bool ReplayGate::suppresses(const proto::LinkOp& op) noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    if (level_ == ReplayLevel::Idle) return false;
    proto::PlayerIndex player{};
    if (const auto e = infra::as<proto::LinkOp::JoyEmit>(op)) {
        player = e->player;
    } else if (const auto rl = infra::as<proto::LinkOp::JoyRelease>(op)) {
        player = rl->player;
    } else {
        return false;
    }
    if (player.v >= proto::JoystickPort::kMaxPorts) return false;
    ++status_.pad_drops;
    return true;
}

void ReplayGate::disarm_(ReplayEnd why, bool release) noexcept {

    if (release && level_ == ReplayLevel::Running) {
        if (ready_) {
            write_masks_({}, ports_);
        } else {
            release_ports_ = static_cast<std::uint8_t>(release_ports_ | ports_);
        }
    }
    level_ = ReplayLevel::Idle;
    status_.end = why;
    held_.reset();
    dirty_ = true;
}

}  // namespace mister::app
