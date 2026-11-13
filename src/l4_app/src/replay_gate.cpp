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
    return run_.ref.edge() - run_.began;
}

std::chrono::nanoseconds ReplayGate::into_frame_() const noexcept {
    return now_ - run_.ref.edge();
}

std::chrono::nanoseconds ReplayGate::since_offset_() const noexcept {
    return into_frame_() - facts_.offset;
}

Ex<proto::FrameCount> ReplayGate::read_counter_() noexcept {
    round_words_ += proto::FrameCounterRead::kReadWords;
    return w_.frames.read();
}

Wrapping ReplayGate::wrap_(proto::FrameCount c) noexcept {
    return {CoreFrameCount{c.count}, CoreFrameCount{proto::FrameCounterRead::kWrapMask}};
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
        && (late.count - run_.blk_base != status_.blk_late || late_max > status_.blk_late_max_us)) {
        status_.blk_late = late.count - run_.blk_base;
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
        run_.tick.reset();
        run_.gap = {};
        return;
    }
    run_.gap = run_.tick ? now_ - *run_.tick : std::chrono::nanoseconds{};
    run_.tick = now_;
    note_gap_();
    if (!sample_ref_())
        return;
    if (!run_.epoch) {
        if (facts_.epoch.by_silence() && now_ - run_.began > kSilenceWindowNs)
            disarm_(ReplayEnd::EpochAmbiguous, true);
        return;
    }
    run_.mf = WideMovieFrame{}
        + (CoreFrameGap::of_count(run_.ref.frames()) - CoreFrameGap::of_count(*run_.epoch))
              .movie_span()
        - facts_.lead;
    note_depth_();
    if (run_.held && due_(*run_.held)) {
        const ReplayMsg::Input r = *run_.held;
        run_.held.reset();
        apply_(r);
    }
}

bool ReplayGate::sample_ref_() noexcept {

    if (run_.ref.lost(now_, facts_.alias)) {
        disarm_(ReplayEnd::RefLost, true);
        return false;
    }
    FrameReference::Step step{};
    if (status_.ref == ReplayRef::Counter) {
        const auto r = read_counter_();
        if (!r || !r->supported) {
            ++status_.link_errors;
            return false;
        }
        step = run_.ref.sample(wrap_(*r), now_, dating_());
    } else {
        reactor::FrameRecord rec{};
        if (!w_.vsync || w_.vsync->sample_into(rec) == 0)
            return false;

        step = run_.ref.sample(FrameReference::VsyncSample{rec.seq, rec.edge}, now_);
    }
    if (!step.moved())
        return true;
    if (!step.single())
        status_.skipped_edges += step.skipped();
    if (!run_.epoch) {
        if (facts_.epoch.by_silence()) {
            decide_silence_(step);
        } else {
            decide_epoch_(step);
        }
    }
    return level_ == ReplayLevel::Running;
}

infra::OptRef<const FrameReference::Dating> ReplayGate::dating_() const noexcept {
    return run_.epoch ? infra::OptRef<const FrameReference::Dating>{facts_.dating}
                      : infra::OptRef<const FrameReference::Dating>{};
}

namespace {

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

void ReplayGate::decide_epoch_(FrameReference::Step step) noexcept {
    const std::chrono::nanoseconds d = end_to_edge_();
    status_.epoch_delay_us = us_of(d);
    const CountParity first = run_.ref.parity();
    const EpochRule::Pick p = facts_.epoch.pick(d, first, facts_.tick);
    const bool untimed = facts_.untimed(p);

    const std::optional<CoreFrameCount> slip = (untimed || p.j < CoreFrameGap{})
        ? facts_.epoch.slip(d, first, facts_.tick)
        : std::nullopt;
    if (!slip && (!step.single() || p.j < CoreFrameGap{} || untimed || p.ambiguous())) {

        status_.epoch_fail = !step.single() ? EpochFail::Retry
            : untimed                       ? EpochFail::Untimed
            : p.j < CoreFrameGap{}          ? EpochFail::Before
                                            : EpochFail::Retry;
        return disarm_(ReplayEnd::EpochAmbiguous, true);
    }

    run_.epoch = slip ? *slip : (CoreFrameGap{1} + p.j).core_frames();
    status_.epoch_edge = *run_.epoch;
}

void ReplayGate::decide_silence_(FrameReference::Step step) noexcept {
    const std::chrono::nanoseconds gap = run_.ref.lapse_since_look();
    if (gap <= facts_.epoch.period + kSilenceSlackNs)
        return;

    if (!step.single())
        return disarm_(ReplayEnd::EpochAmbiguous, true);
    status_.epoch_delay_us = us_of(end_to_edge_());
    run_.epoch = run_.ref.frames();
    status_.epoch_edge = *run_.epoch;
}

bool ReplayGate::pre_epoch_(MovieFrame frame) const noexcept {
    return wide_frame(frame) + facts_.lead < facts_.pre_epoch_below;
}

bool ReplayGate::live_() const noexcept {
    return run_.epoch && run_.ref.frames() + facts_.floor_back >= *run_.epoch;
}

bool ReplayGate::due_(const ReplayMsg::Input& r) const noexcept {

    if (!ready_ || level_ != ReplayLevel::Running || !live_())
        return false;
    const WideMovieFrame first = wide_frame(r.first);
    return first < run_.mf || (first == run_.mf && since_offset_() >= std::chrono::nanoseconds{0});
}

void ReplayGate::note_late_(MovieFrame first) noexcept {
    ++status_.late;
    if (status_.first_late_frame >= MovieFrame{})
        return;
    status_.first_late_frame = first;
    status_.late_gap_us = us_of(run_.gap);
    status_.late_into_us = us_of(into_frame_());
    status_.late_edge_us = us_of(run_.ref.edge_gap());
    status_.late_depth = queued_.value_or(0);
}

void ReplayGate::note_gap_() noexcept {
    const StatusMicros us = us_of(run_.gap);
    if (us > status_.gap_max_us) {
        status_.gap_max_us = us;
        status_.gap_frame = run_.mf.narrowed();
    }
}

void ReplayGate::note_depth_() noexcept {
    if (!queued_ || run_.end || !live_())
        return;
    const auto depth = static_cast<std::int32_t>(*queued_ + (run_.held ? 1u : 0u));
    if (status_.depth_min < 0 || depth < status_.depth_min) {
        status_.depth_min = depth;
        status_.depth_min_frame = run_.mf.narrowed();
    }
}

void ReplayGate::apply_(const ReplayMsg::Input& r) noexcept {
    const WideMovieFrame first = wide_frame(r.first);
    const WideMovieFrame last = wide_frame(r.last);
    run_.covered = std::max(run_.covered, last);
    if (pre_epoch_(r.first)) {
        ++status_.pre_epoch;
        write_(r);
        return;
    }

    const WideMovieFrame level = since_offset_() >= std::chrono::nanoseconds{0}
        ? run_.mf
        : run_.mf - MovieSpan{1};
    if (last < level) {
        ++status_.lost;
        return note_late_(r.first);
    }
    if (first < run_.mf) {
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
    write_masks_(r.mask, facts_.ports);
    status_.applied_frame = std::min(run_.mf, wide_frame(r.last)).narrowed();
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
    return !run_.held && !run_.end;
}

void ReplayGate::on(const ReplayMsg::Arm& a, const ReplayMsg::Head& h) noexcept {
    TASTY_SEAT_BODY(ReplayGate);

    sample_control_();

    const std::optional<ArmFacts> facts = ArmFacts::of(a);
    if (level_ != ReplayLevel::Idle
        || !release_ports_.empty()
        || ctrl_.op != ReplayOp::Play
        || ctrl_.gen != h.gen
        || !facts) {
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
    facts_ = *facts;
    run_ = Run{};
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
    run_.held = r;
}

void ReplayGate::on(const ReplayMsg::End& e, const ReplayMsg::Head& h) noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    if (level_ == ReplayLevel::Idle || h.gen != gen_) {
        ++status_.stale;
        dirty_ = true;
        return;
    }
    run_.end = e.frame;
}

void ReplayGate::misrouted(const ReplayMsg&) noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    ++status_.stale;
    dirty_ = true;
}

void ReplayGate::settle() noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    if (level_ == ReplayLevel::Running && live_() && run_.mf >= WideMovieFrame{}) {
        if (run_.end && !run_.held && run_.mf >= wide_frame(*run_.end)) {
            if (run_.mf > wide_frame(*run_.end) || since_offset_() >= std::chrono::nanoseconds{0})
                disarm_(ReplayEnd::Finished, true);
        } else if (!run_.held && run_.mf > run_.covered && run_.mf != run_.underrun_at) {

            const WideMovieFrame at = run_.mf;
            run_.underrun_at = at;
            ++status_.underruns;
            if (status_.first_underrun_frame < MovieFrame{})
                status_.first_underrun_frame = run_.mf.narrowed();
        }
    }

    if (level_ == ReplayLevel::Idle && (!dirty_ || !release_ports_.empty()))
        return;
    status_.level = level_;
    status_.movie_frame = run_.mf.narrowed();
    w_.status.publish(status_);
    dirty_ = false;
}

void ReplayGate::note_download_end(proto::IoIndex wire_index) noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    if (level_ != ReplayLevel::AwaitPowerOn || facts_.event != ReplayMsg::PowerOnEvent::LoadEnd)
        return;
    if (!facts_.admits(wire_index))
        return;
    begin_run_();
}

void ReplayGate::note_core_reset() noexcept {
    TASTY_SEAT_BODY(ReplayGate);
    if (level_ != ReplayLevel::AwaitPowerOn || facts_.event != ReplayMsg::PowerOnEvent::ResetPulse)
        return;
    begin_run_();
}

void ReplayGate::begin_run_() noexcept {
    now_ = w_.clock.now();
    const auto c = read_counter_();
    if (c && c->supported) {
        status_.ref = ReplayRef::Counter;
        run_.ref = FrameReference::at(wrap_(*c), now_);
    } else {
        reactor::FrameRecord rec{};

        if (!facts_.vsync_ok
            || facts_.epoch.needs_counter()
            || !w_.vsync
            || w_.vsync->sample_into(rec) == 0) {
            return disarm_(ReplayEnd::NoReference, false);
        }
        status_.ref = ReplayRef::Vsync;
        run_.ref = FrameReference::at(Wrapping::of(rec.seq), now_);
    }
    run_.began = now_;
    run_.blk_base = blk_seen_;
    level_ = ReplayLevel::Running;
    dirty_ = true;
}

std::optional<ReplayGate::ArmFacts> ReplayGate::ArmFacts::of(const ReplayMsg::Arm& a) noexcept {
    if (a.ports.empty() || a.period_ns == CodecNs::zero())
        return std::nullopt;
    ArmFacts f;
    f.ports = a.ports.limited_to(svc::PlayerCount::of(kReplayPorts));
    f.rom = a.rom_index;
    f.vsync_ok = a.vsync_ok;
    f.lead = a.lead.movie_span();
    f.offset = std::chrono::duration_cast<std::chrono::nanoseconds>(a.offset_us);
    f.tick = kTickNs;
    f.epoch = EpochRule{
        .poweron = a.poweron_ns,
        .period = a.period_ns,
        .line0 = a.line0_ns,
        .p0 = a.p0,
        .late_frames = a.late_frames
    };
    f.event = a.event;
    f.floor_back = f.epoch.by_silence() ? CoreFrameCount{} : kFloorBack;
    f.pre_epoch_below = WideMovieFrame{} - CoreFrameGap::of_count(f.floor_back).movie_span();
    f.alias = kAliasFrames.lasting(f.epoch.period);
    f.dating = {f.epoch.period, kGapNs};
    return f;
}

bool ReplayGate::ArmFacts::admits(proto::IoIndex wire) const noexcept {
    return rom == kNoRomSlot || proto::WideIoIndex::of(wire, 0).io() == rom;
}

bool ReplayGate::ArmFacts::untimed(const EpochRule::Pick& p) const noexcept {
    return p.j == CoreFrameGap{}
    && lead <= MovieSpan{} - CoreFrameGap::of_count(kFloorBack).movie_span();
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
            write_masks_({}, facts_.ports);
        } else {
            release_ports_ = release_ports_.with(facts_.ports);
        }
    }
    level_ = ReplayLevel::Idle;
    status_.end = why;
    run_.held.reset();
    dirty_ = true;
}

}
