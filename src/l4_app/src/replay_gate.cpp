// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/replay_gate.h"

#include <algorithm>
#include <utility>

#include "infra/message_sum.h"
#include "app/link_op_dispatch.h"
#include "app/log_clock.h"
#include "proto/spi_fio_queue.h"
#include "svc/input_emitter.h"

namespace mister::app {

static_assert(
    ReplayGate::kMaxRoundWords <= proto::tightest_round_budget_words(),
    "a replay round's own words never exceed the round budget they are taken from"
);

std::chrono::nanoseconds ReplayGate::end_to_edge_() const noexcept {
    return edge_ - end_;
}

std::chrono::nanoseconds ReplayGate::into_frame_() const noexcept {
    return now_ - edge_;
}

std::chrono::nanoseconds ReplayGate::since_offset_() const noexcept {
    return into_frame_() - offset_ns_;
}

Ex<proto::FrameCount> ReplayGate::read_counter_() noexcept {
    round_words_ += proto::FrameCounterRead::kReadWords;
    return w_.frames.read();
}

void ReplayGate::sample_control_() noexcept {
    ReplayControl c{};

    if (control_seen_.take_if_changed(w_.control, c))
        ctrl_ = c;
}

void ReplayGate::tick(
    bool ready,
    CoreEdgeSeq core_edge_seq,
    proto::LateAnswers late,
    std::optional<std::uint32_t> queued
) noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    sample_control_();
    queued_ = queued;
    blk_seen_ = late.count;
    const StatusMicros late_max{late.max_us};
    if (level_ == ReplayLevel::Running
        && (late.count - blk_base_ != status_.blk_late || late_max > status_.blk_late_max_us)) {
        status_.blk_late = late.count - blk_base_;
        status_.blk_late_max_us = std::max(status_.blk_late_max_us, late_max);
        dirty_ = true;
    }

    if (!release_ports_.empty() && (core_edge_seq != edge_seq_ || ready)) {
        if (core_edge_seq == edge_seq_)
            force_release_(release_ports_);
        release_ports_ = {};
        dirty_ = true;
    }
    if (level_ == ReplayLevel::Idle) {
        edge_seq_ = core_edge_seq;
        return;
    }
    ready_ = ready;

    if (core_edge_seq != edge_seq_)
        return disarm_(ReplayEnd::CoreSwitch, false);
    if (ctrl_.gen != gen_)
        return disarm_(ReplayEnd::Superseded, true);
    if (ctrl_.op != ReplayOp::Play)
        return disarm_(ReplayEnd::Stopped, true);
    now_ = w_.clock.now();

    if (level_ == ReplayLevel::AwaitPowerOn)
        return;
    if (!ready) {
        ++status_.held_rounds;
        tick_.reset();
        gap_ = {};
        return;
    }
    gap_ = tick_ ? now_ - *tick_ : std::chrono::nanoseconds{};
    tick_ = now_;
    note_gap_();
    if (!sample_ref_())
        return;
    if (!epoch_known_) {
        if (rule_.p0 == ReplayMsg::P0Parity::AfterSilence && now_ - end_ > kSilenceWindowNs)
            disarm_(ReplayEnd::EpochAmbiguous, true);
        return;
    }
    mf_ = WideMovieFrame{static_cast<std::int64_t>(frames_) - epoch_} - lead_;
    note_depth_();
    if (held_ && due_(*held_)) {
        const ReplayMsg::Input r = *held_;
        held_.reset();
        apply_(r);
    }
}

bool ReplayGate::sample_ref_() noexcept {

    if (now_ - last_sample_ > alias_ns_) {
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
        if (!w_.vsync || w_.vsync->sample_into(rec) == 0)
            return false;
        raw = rec.seq.v;
    }
    const std::optional<os::RawMonotonicClock::time_point> prev_read = read_;
    read_ = now_;
    if (raw == last_raw_)
        return true;
    last_sample_ = now_;
    const std::uint32_t d = (raw - last_raw_) & wrap_mask_;
    last_raw_ = raw;
    frames_ += d;
    os::RawMonotonicClock::time_point edge = now_;

    if (epoch_known_ && prev_read && now_ - *prev_read > kGapNs)
        edge = std::min(now_, std::max(*prev_read, edge_ + rule_.period * d));
    edge_gap_ = edge - edge_;
    edge_ = edge;
    if (d > 1)
        status_.skipped_edges += d - 1;
    if (!epoch_known_) {
        if (rule_.p0 == ReplayMsg::P0Parity::AfterSilence) {
            decide_silence_(d);
        } else {
            decide_epoch_(d);
        }
    }
    return level_ == ReplayLevel::Running;
}

namespace {

std::int64_t ceil_div(std::chrono::nanoseconds a, std::chrono::nanoseconds b) {
    const std::int64_t periods = a / b;
    return a % b == std::chrono::nanoseconds::zero() || a < std::chrono::nanoseconds::zero()
        ? periods
        : periods + 1;
}

std::int64_t released(std::int64_t j, ReplayMsg::P0Parity p0, std::uint32_t first_count) {
    if (p0 == ReplayMsg::P0Parity::Any || j < 0)
        return j;
    const std::uint32_t want = p0 == ReplayMsg::P0Parity::Odd ? 1u : 0u;
    return ((first_count + static_cast<std::uint32_t>(j)) & 1u) == want ? j : j + 1;
}

struct PadOwned {
    using Result = bool;

    static constexpr proto::PlayerIndex kPortLimit{proto::JoystickPort::kMaxPorts};

    static bool joy_port(proto::PlayerIndex player) noexcept { return player < kPortLimit; }

    bool on(const proto::LinkOp::JoyEmit& a) const noexcept { return joy_port(a.player); }

    bool on(const proto::LinkOp::JoyRelease& a) const noexcept { return joy_port(a.player); }

    bool misrouted(const proto::LinkOp&) const noexcept { return false; }
};

static_assert(LinkOpPadSink<PadOwned>);

}

ReplayGate::EpochPick ReplayGate::EpochRule::pick(
    std::chrono::nanoseconds delay,
    std::uint32_t first_count
) const noexcept {
    const std::chrono::nanoseconds x = poweron + period - line0 - delay;
    return {
        released(ceil_div(x - ReplayGate::kEndSlackNs, period), p0, first_count),
        released(ceil_div(x + ReplayGate::kTickNs, period), p0, first_count)
    };
}

void ReplayGate::decide_epoch_(std::uint32_t delta) noexcept {
    const std::chrono::nanoseconds d = end_to_edge_();
    status_.epoch_delay_us = us_of(d);
    const EpochPick p = rule_.pick(d, last_raw_);
    const bool band = p.j != p.hi;

    const bool untimed = p.j == 0 && lead_ <= MovieSpan{-std::int64_t{kFloorBack}};

    bool slipped = false;
    std::uint32_t slip = 0;
    if ((untimed || p.j < 0) && rule_.p0 == ReplayMsg::P0Parity::Any && rule_.late_frames.any()) {
        for (const std::uint32_t k : rule_.late_frames.slips()) {
            const std::chrono::nanoseconds earlier = d - rule_.period * k;
            const EpochPick s = rule_.pick(earlier, last_raw_);
            if (s.j == s.hi && s.j == k) {
                slipped = true;
                slip = k;
                break;
            }
        }
    }
    if (!slipped && (delta != 1 || p.j < 0 || untimed || band)) {

        status_.epoch_fail = delta != 1 ? EpochFail::Retry
            : untimed                   ? EpochFail::Untimed
            : p.j < 0                   ? EpochFail::Before
                                        : EpochFail::Retry;
        return disarm_(ReplayEnd::EpochAmbiguous, true);
    }

    epoch_ = slipped ? slip : static_cast<std::uint32_t>(1 + p.j);
    status_.epoch_edge = epoch_;
    epoch_known_ = true;
}

void ReplayGate::decide_silence_(std::uint32_t delta) noexcept {
    const std::chrono::nanoseconds gap = edge_ - prev_edge_;
    prev_edge_ = edge_;
    if (gap <= rule_.period + kSilenceSlackNs)
        return;

    if (delta != 1)
        return disarm_(ReplayEnd::EpochAmbiguous, true);
    status_.epoch_delay_us = us_of(end_to_edge_());
    epoch_ = frames_;
    status_.epoch_edge = epoch_;
    epoch_known_ = true;
}

bool ReplayGate::pre_epoch_(MovieFrame frame) const noexcept {
    return wide_frame(frame) + lead_ < WideMovieFrame{-static_cast<std::int64_t>(floor_back_)};
}

bool ReplayGate::live_() const noexcept {
    return epoch_known_ && frames_ + floor_back_ >= epoch_;
}

bool ReplayGate::due_(const ReplayMsg::Input& r) const noexcept {

    if (!ready_ || level_ != ReplayLevel::Running || !live_())
        return false;
    const WideMovieFrame first = wide_frame(r.first);
    return first < mf_ || (first == mf_ && since_offset_() >= std::chrono::nanoseconds{0});
}

void ReplayGate::note_late_(MovieFrame first) noexcept {
    ++status_.late;
    if (status_.first_late_frame >= MovieFrame{})
        return;
    status_.first_late_frame = first;
    status_.late_gap_us = us_of(gap_);
    status_.late_into_us = us_of(into_frame_());
    status_.late_edge_us = us_of(edge_gap_);
    status_.late_depth = queued_.value_or(0);
}

void ReplayGate::note_gap_() noexcept {
    const StatusMicros us = us_of(gap_);
    if (us > status_.gap_max_us) {
        status_.gap_max_us = us;
        status_.gap_frame = mf_.narrowed();
    }
}

void ReplayGate::note_depth_() noexcept {
    if (!queued_ || end_seen_ || !live_())
        return;
    const auto depth = static_cast<std::int32_t>(*queued_ + (held_ ? 1u : 0u));
    if (status_.depth_min < 0 || depth < status_.depth_min) {
        status_.depth_min = depth;
        status_.depth_min_frame = mf_.narrowed();
    }
}

void ReplayGate::apply_(const ReplayMsg::Input& r) noexcept {
    const WideMovieFrame first = wide_frame(r.first);
    const WideMovieFrame last = wide_frame(r.last);
    covered_ = std::max(covered_, last);
    if (pre_epoch_(r.first)) {
        ++status_.pre_epoch;
        write_(r);
        return;
    }

    const WideMovieFrame level = since_offset_() >= std::chrono::nanoseconds{0}
        ? mf_
        : mf_ - MovieSpan{1};
    if (last < level) {
        ++status_.lost;
        return note_late_(r.first);
    }
    if (first < mf_) {
        note_late_(r.first);
    } else {
        const StatusMicros us = us_of(into_frame_());
        if (since_offset_() > kSlipNs)
            ++status_.delayed;
        status_.apply_min_us = status_.on_time == 0 ? us : std::min(status_.apply_min_us, us);
        status_.apply_max_us = std::max(status_.apply_max_us, us);
        ++status_.on_time;
    }
    write_(r);
}

void ReplayGate::write_(const ReplayMsg::Input& r) noexcept {
    write_masks_(r.mask, ports_);
    status_.applied_frame = std::min(mf_, wide_frame(r.last)).narrowed();
}

void ReplayGate::write_masks_(
    const std::array<proto::JoyMask, kReplayPorts>& mask,
    svc::PlayerMask ports
) noexcept {
    proto::JoystickPort& joy = w_.emitter.joysticks();
    for (std::uint8_t p = 0; p < kReplayPorts; ++p) {
        const proto::PlayerIndex slot{p};
        if (!ports.has(slot))
            continue;
        const proto::PlayerIndex port = joy.wire_port(slot);
        const std::uint32_t words = w_.emitter.link_words_bound(port);
        const std::uint32_t held = w_.emitter.pad_facts_held();
        const auto out = w_.emitter.submit(frame_, port, mask[p], proto::JoyMask{});
        if (!out || *out)
            round_words_ += words;
        count_write_(out, held);
    }
}

void ReplayGate::force_release_(svc::PlayerMask ports) noexcept {
    proto::JoystickPort& joy = w_.emitter.joysticks();
    for (std::uint8_t p = 0; p < kReplayPorts; ++p) {
        const proto::PlayerIndex slot{p};
        if (!ports.has(slot))
            continue;
        const proto::PlayerIndex port = joy.wire_port(slot);
        const std::uint32_t words = w_.emitter.link_words_bound(port);
        const std::uint32_t held = w_.emitter.pad_facts_held();
        auto out = w_.emitter.submit(frame_, port, proto::JoyMask{}, proto::JoyMask{});
        if (out && !*out)
            out = w_.emitter.release(frame_, port, proto::JoyMask{~0u});
        if (!out || *out)
            round_words_ += words;
        count_write_(out, held);
    }
}

void ReplayGate::count_write_(const Ex<bool>& out, std::uint32_t held_before) noexcept {
    if (!out) {
        ++status_.link_errors;
    } else if (w_.emitter.pad_facts_held() != held_before) {
        ++status_.pad_held;
    } else if (*out) {
        ++status_.writes;
    }
}

bool ReplayGate::wants_record() const noexcept {
    if (level_ == ReplayLevel::Idle)
        return true;
    return !held_ && !end_seen_;
}

void ReplayGate::on(const ReplayMsg::Arm& a, const ReplayMsg::Head& h) noexcept {
    TASTY_SEAT_BODY(ReplayGate);

    sample_control_();

    if (level_ != ReplayLevel::Idle
        || !release_ports_.empty()
        || ctrl_.op != ReplayOp::Play
        || ctrl_.gen != h.gen
        || a.ports.empty()
        || a.period_ns == CodecNs::zero()) {
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
    ports_ = a.ports.limited_to(svc::PlayerCount::of(kReplayPorts));
    rom_index_ = a.rom_index;
    vsync_ok_ = a.vsync_ok;
    lead_ = a.lead.movie_span();
    offset_ns_ = std::chrono::duration_cast<std::chrono::nanoseconds>(a.offset_us);
    rule_ = EpochRule{
        .poweron = a.poweron_ns,
        .period = a.period_ns,
        .line0 = a.line0_ns,
        .p0 = a.p0,
        .late_frames = a.late_frames
    };
    event_ = a.event;

    floor_back_ = rule_.p0 == ReplayMsg::P0Parity::AfterSilence ? 0u : kFloorBack;
    alias_ns_ = rule_.period * kAliasFrames;
    epoch_known_ = false;
    epoch_ = 0;
    frames_ = 0;
    read_.reset();
    edge_gap_ = {};
    tick_.reset();
    gap_ = {};
    mf_ = kNoWideMovieFrame;
    covered_ = kNoWideMovieFrame;
    underrun_at_ = kNoWideMovieFrame;
    held_.reset();
    end_seen_ = false;
    end_frame_ = MovieFrame{};
    dirty_ = true;
}

void ReplayGate::on(const ReplayMsg::Input& r, const ReplayMsg::Head& h) noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    if (level_ == ReplayLevel::Idle || h.gen != gen_ || r.last < r.first) {
        ++status_.stale;
        dirty_ = true;
        return;
    }
    if (due_(r))
        return apply_(r);
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
    if (level_ == ReplayLevel::Running && live_() && mf_ >= WideMovieFrame{}) {
        if (end_seen_ && !held_ && mf_ >= wide_frame(end_frame_)) {
            if (mf_ > wide_frame(end_frame_) || since_offset_() >= std::chrono::nanoseconds{0})
                disarm_(ReplayEnd::Finished, true);
        } else if (!held_ && mf_ > covered_ && mf_ != underrun_at_) {

            const WideMovieFrame at = mf_;
            underrun_at_ = at;
            ++status_.underruns;
            if (status_.first_underrun_frame < MovieFrame{})
                status_.first_underrun_frame = mf_.narrowed();
        }
    }

    if (level_ == ReplayLevel::Idle && (!dirty_ || !release_ports_.empty()))
        return;
    status_.level = level_;
    status_.movie_frame = mf_.narrowed();
    w_.status.publish(status_);
    dirty_ = false;
}

void ReplayGate::note_download_end(proto::IoIndex wire_index) noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    if (level_ != ReplayLevel::AwaitPowerOn || event_ != ReplayMsg::PowerOnEvent::LoadEnd)
        return;

    if (rom_index_ != kNoRomSlot && proto::WideIoIndex::of(wire_index, 0).io() != rom_index_)
        return;
    begin_run_();
}

void ReplayGate::note_core_reset() noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    if (level_ != ReplayLevel::AwaitPowerOn || event_ != ReplayMsg::PowerOnEvent::ResetPulse)
        return;
    begin_run_();
}

void ReplayGate::begin_run_() noexcept {
    now_ = w_.clock.now();
    end_ = now_;
    const auto c = read_counter_();
    if (c && c->supported) {
        status_.ref = ReplayRef::Counter;
        last_raw_ = c->count;
        wrap_mask_ = proto::FrameCounterRead::kWrapMask;
    } else {
        reactor::FrameRecord rec{};

        if (!vsync_ok_
            || rule_.p0 != ReplayMsg::P0Parity::Any
            || !w_.vsync
            || w_.vsync->sample_into(rec) == 0) {
            return disarm_(ReplayEnd::NoReference, false);
        }
        status_.ref = ReplayRef::Vsync;
        last_raw_ = rec.seq.v;
        wrap_mask_ = 0xFFFF'FFFFu;
        alias_ns_ = rule_.period * kAliasFrames;
    }
    frames_ = 0;
    edge_ = now_;
    prev_edge_ = now_;
    last_sample_ = now_;
    read_ = now_;
    blk_base_ = blk_seen_;
    level_ = ReplayLevel::Running;
    dirty_ = true;
}

bool ReplayGate::suppresses(const proto::LinkOp& op) noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    if (level_ == ReplayLevel::Idle)
        return false;
    PadOwned pad;
    const bool drop = infra::dispatch<LinkOpPadRoutes>(op, pad);
    if (drop)
        ++status_.pad_drops;
    return drop;
}

void ReplayGate::disarm_(ReplayEnd why, bool release) noexcept {

    if (release && level_ == ReplayLevel::Running) {
        if (ready_) {
            write_masks_({}, ports_);
        } else {
            release_ports_ = release_ports_.with(ports_);
        }
    }
    level_ = ReplayLevel::Idle;
    status_.end = why;
    held_.reset();
    dirty_ = true;
}

}
