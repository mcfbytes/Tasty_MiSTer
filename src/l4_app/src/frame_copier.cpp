// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_copier.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

#include "app/log_clock.h"
#include "app/rec_options.h"

namespace mister::app {

namespace {

using hal::ScalerBuffers;
using hal::ScalerHeader;

constexpr std::int64_t kMsNs = 1'000'000;

constexpr std::int64_t kLowlatGuardNs = 2 * kMsNs;

[[nodiscard]] os::RawMonotonicClock::time_point raw_instant(
    std::chrono::nanoseconds since_epoch
) noexcept {
    return os::RawMonotonicClock::time_point{since_epoch};
}

}

void FrameCopier::serve() noexcept {
    TASTY_SEAT_BODY(FrameCopier);
    reap_();
    drain_control_();
    std::int64_t now = w_.clock.now().time_since_epoch().count();
    switch (st_.state) {
        case RecState::Idle:
            break;
        case RecState::Probing:
            probe_step_(now);
            break;
        case RecState::Armed:
        case RecState::Recording:
            watch_step_();
            if (st_.state == RecState::Recording && !gen_.open_sent && !open_segment_())
                break;
            if (st_.state == RecState::Recording && gen_.regrow_bytes != 0 && !regrow_())
                break;
            if (st_.state == RecState::Armed || st_.state == RecState::Recording) {
                if (gen_.lowlat) {
                    now = approach_(now);
                    poll_lowlat_(now);
                } else {
                    poll_triple_(now);
                }
            }
            break;
        case RecState::Closing:
            close_step_();
            break;
    }
    if (dirty_)
        w_.status.publish(st_);
    dirty_ = false;
}

bool FrameCopier::idle() const noexcept {
    return requests_.outbound() == 0;
}

infra::ParkTimeout FrameCopier::park_ms() const noexcept {
    if (st_.state == RecState::Idle)
        return infra::kForever;
    const bool live = st_.state == RecState::Armed || st_.state == RecState::Recording;
    if (live && gen_.lowlat && gen_.ll_waiting) {
        const std::int64_t left = ll_due_ns_ - w_.clock.now().time_since_epoch().count();

        if (left < kMsNs)
            return infra::kNoWait;
        const auto whole = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::nanoseconds{left}
        );
        return std::chrono::duration_cast<infra::ParkTimeout>(
            std::min(whole, std::chrono::milliseconds{kPollMs})
        );
    }
    return kPollMs;
}

void FrameCopier::release() noexcept {
    TASTY_SEAT_BODY(FrameCopier);
    reap_();
    if (st_.state != RecState::Idle && st_.state != RecState::Closing)
        end_(RecVerdict::Stopped);
    if (st_.state == RecState::Closing)
        close_step_();
    set_demand_(false);
    w_.status.publish(st_);
}

void FrameCopier::drain_control_() noexcept {
    for (;;) {
        auto job = requests_.take();
        if (!job)
            return;
        take_request_(std::move(job));
    }
}

void FrameCopier::take_request_(Chan::Job job) noexcept {
    switch (job->op) {
        case RecOp::Start:
        case RecOp::Arm:
            if (st_.state != RecState::Idle)
                return answer_(job, RecVerdict::Busy);
            return begin_(job);
        case RecOp::Stop:
            answer_(job, RecVerdict::Stopped);
            if (st_.state != RecState::Idle && st_.state != RecState::Closing)
                end_(RecVerdict::Stopped);
            return;
        case RecOp::Disarm: {
            const bool live = st_.state != RecState::Idle && st_.state != RecState::Closing;
            if (live && !gen_.from_arm)
                return answer_(job, RecVerdict::Busy);
            answer_(job, RecVerdict::Stopped);
            if (live)
                end_(RecVerdict::Stopped);
            return;
        }
        case RecOp::None:
            return;
    }
}

void FrameCopier::answer_(Chan::Job& job, RecVerdict v) noexcept {
    job->verdict = v;
    job.complete();
    dirty_ = true;
}

void FrameCopier::answer_pending_(RecVerdict v) noexcept {
    if (!answering_)
        return;
    (**answering_).verdict = v;
    answering_->complete();
    answering_.reset();
}

void FrameCopier::begin_(Chan::Job& job) noexcept {
    if (!window_)
        return answer_(job, RecVerdict::NoWindow);
    if (job->mode == RecMode::Avi && !rec_codec_available(job->opt.codec))
        return answer_(job, RecVerdict::NoCodec);
    st_ = RecorderStatus{};
    st_.gen = job->gen;
    st_.state = RecState::Probing;
    gen_ = RecGeneration{};
    gen_.path = job->path;
    gen_.mode = job->mode;
    gen_.opt = job->opt;
    st_.avi = job->mode == RecMode::Avi;
    gen_.from_arm = job->op == RecOp::Arm;
    numbering_.restart();
    ReplayStatus rs{};
    if (gen_.from_arm && w_.replay.sample_into(rs) != 0)
        note_replay_(rs);
    const auto t0 = w_.clock.now();
    probe_.restart(t0);
    armed_ns_ = t0.time_since_epoch().count();
    set_demand_(true);
    probe_.read(*window_);

    answering_ = std::move(job);
    dirty_ = true;
}

void FrameCopier::end_(RecVerdict why) noexcept {
    answer_pending_(why);
    st_.end = why;
    st_.state = RecState::Closing;
    gen_.ll_waiting = false;
    dirty_ = true;
    close_step_();
}

void FrameCopier::set_demand_(bool on) noexcept {
    if (on == demand_on_)
        return;
    demand_on_ = on;
    w_.demand.publish(FrameDemand{.on = on});
}

void FrameCopier::reap_() noexcept {
    for (std::size_t i = 0; i < kRawFrameSlots; ++i) {
        auto back = w_.channel.reap();
        if (!back)
            break;
    }
}

bool FrameCopier::all_home_() const noexcept {
    const auto c = w_.channel.census();
    return c.idle == c.live;
}

void FrameCopier::probe_step_(std::int64_t now) noexcept {

    if (ReplayStatus rs{}; gen_.from_arm && w_.replay.sample_into(rs) != 0)
        note_replay_(rs);
    probe_.read(*window_);
    const auto d = probe_.decide(raw_instant(std::chrono::nanoseconds{now}));
    if (d == hal::ScalerProbe::Verdict::Pending)
        return;
    if (d == hal::ScalerProbe::Verdict::Dead) {
        if (gen_.from_arm && now - armed_ns_ < kEpochWaitNs && replay_before_epoch_()) {
            probe_.restart(raw_instant(std::chrono::nanoseconds{now}));
            return;
        }
        const RecVerdict v = hal::ScalerProbe::port_stuck(*window_) ? RecVerdict::ScalerPortStuck
                                                                    : RecVerdict::NoLiveBuffer;
        answer_pending_(v);
        end_(v);
        return;
    }
    const auto layout = hal::ScalerProbe::layout_of(d);
    if (!layout)
        return;
    gen_.lowlat = layout->lowlat;
    gen_.stride = layout->stride;
    st_.stride_mib = hal::stride_mib(gen_.stride);
    st_.lowlat = gen_.lowlat;
    dirty_ = true;

    track_reset_(probe_.moved(gen_.stride), now);

    std::size_t bytes = 0;
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
        const auto h = window_->header(base_(i));
        if (live_[i] && h && h->supported())
            bytes = std::max(bytes, h->image_bytes());
    }
    if (!reserve_arena_(bytes)) {
        answer_pending_(RecVerdict::NoMemory);
        return end_(RecVerdict::NoMemory);
    }
    answer_pending_(RecVerdict::Started);
    st_.state = gen_.from_arm ? RecState::Armed : RecState::Recording;
    if (!gen_.from_arm)
        (void)open_segment_();
}

bool FrameCopier::open_segment_() noexcept {
    auto loan = w_.channel.acquire();
    if (!loan)
        return false;
    *loan = infra::make<RawFrameSlot>(
        RawFrameSlot::Open{.opt = gen_.opt, .path = gen_.path, .mode = gen_.mode},
        st_.gen
    );
    w_.channel.send(std::move(loan));
    gen_.open_sent = true;
    gen_.core_seq_known = false;
    st_.state = RecState::Recording;
    dirty_ = true;
    return true;
}

void FrameCopier::close_step_() noexcept {
    if (gen_.open_sent && !gen_.close_sent) {
        auto loan = w_.channel.acquire();
        if (!loan)
            return;
        RawFrameSlot::Close close{};
        attach_pending_(close);
        *loan = infra::make<RawFrameSlot>(close, st_.gen);
        w_.channel.send(std::move(loan));
        gen_.close_sent = true;
    }
    if (!all_home_())
        return;

    arena_.release();
    st_.arena_kib = 0;
    st_.state = RecState::Idle;
    set_demand_(false);
    dirty_ = true;
}

void FrameCopier::attach_pending_(RawFrameSlot::Close& slot) noexcept {
    if (gen_.npending == 0)
        return;
    slot.runs = gen_.npending;
    WideCoreFrame end{};
    for (std::uint8_t i = 0; i < gen_.npending; ++i) {
        slot.run[i] = pending_[i];
        end = pending_[i].after().core_frame;
        st_.rows += pending_[i].count;
    }
    gen_.npending = 0;
    slot.width = PicturePixels::of(st_.width);
    slot.rgb.rows = PictureRows{st_.height};
    slot.stamp.core_frame = end;
    if (gen_.last_cell)
        slot.stamp.movie_frame = numbering_.movie_of(*gen_.last_cell, end);
    dirty_ = true;
}

bool FrameCopier::regrow_() noexcept {
    if (!all_home_())
        return true;
    const std::size_t want = gen_.regrow_bytes;
    gen_.regrow_bytes = 0;
    if (!reserve_arena_(want)) {
        end_(RecVerdict::NoMemory);
        return false;
    }
    return true;
}

bool FrameCopier::reserve_arena_(std::size_t frame_bytes) noexcept {
    constexpr std::size_t kBytesPerKib = 1024;
    const std::size_t slot_bytes = raw_stripe(frame_bytes);
    const std::size_t want = raw_depth(slot_bytes);

    const auto live = w_.channel.set_live(static_cast<std::uint8_t>(want));
    if (!live && live.error().code != Errc::busy)
        return false;
    const std::size_t depth = live ? want : w_.channel.live();
    if (auto r = arena_.reserve(depth, slot_bytes); !r)
        return false;
    st_.arena_kib = static_cast<std::uint32_t>(arena_.bytes() / kBytesPerKib);
    st_.depth = static_cast<std::uint8_t>(depth);
    dirty_ = true;
    return true;
}

void FrameCopier::watch_step_() noexcept {
    if (st_.state == RecState::Recording) {
        RecWriteStatus ws{};
        if (w_.writer.sample_into(ws) != 0
            && ws.gen == st_.gen
            && ws.state == RecWriteState::Failed)
            return end_(RecVerdict::WriterFailed);
    }
    if (st_.state == RecState::Recording) {
        AviWriteStatus as{};
        if (w_.avi_writer.sample_into(as) != 0
            && as.gen == st_.gen
            && as.state == RecWriteState::Failed)
            return end_(RecVerdict::WriterFailed);
    }
    if (st_.state == RecState::Recording) {
        CoreFrameRecord f{};
        if (w_.frames.sample_into(f) != 0) {
            if (!gen_.core_seq_known) {
                core_seq_ = f.core_seq;
                gen_.core_seq_known = true;
            } else if (f.core_seq != core_seq_) {
                return end_(RecVerdict::CoreSwitch);
            }
        }
    }
    if (gen_.from_arm) {
        ReplayStatus r{};
        if (w_.replay.sample_into(r) == 0)
            return;
        if (st_.state == RecState::Armed)
            note_replay_(r);
        if (r.level == ReplayLevel::Running) {
            gen_.replay_running = true;
            gen_.epoch_wait_ns = -1;
        } else if (r.end == ReplayEnd::EpochAmbiguous || r.level == ReplayLevel::AwaitPowerOn) {
            gen_.replay_running = false;

            const std::int64_t now = w_.clock.now().time_since_epoch().count();
            if (gen_.epoch_wait_ns < 0)
                gen_.epoch_wait_ns = now;
            if (now - gen_.epoch_wait_ns > kEpochWaitNs)
                return end_(RecVerdict::ReplayEnded);
        } else if (gen_.replay_running && st_.state == RecState::Armed) {
            return end_(RecVerdict::ReplayEnded);
        } else if (gen_.replay_running && !gen_.tail_counting) {
            gen_.tail_counting = true;
            gen_.tail_left = kReplayTailFrames;
        }
    }
}

void FrameCopier::note_replay_(const ReplayStatus& r) noexcept {

    if (r.level != ReplayLevel::Running || r.movie_frame < MovieFrame{})
        gen_.covers_zero = true;
}

bool FrameCopier::replay_before_epoch_() const noexcept {
    ReplayStatus r{};
    if (w_.replay.sample_into(r) == 0)
        return false;
    if (r.level == ReplayLevel::AwaitPowerOn)
        return true;
    if (r.level == ReplayLevel::Idle
        && (r.end == ReplayEnd::None || r.end == ReplayEnd::EpochAmbiguous))
        return true;
    return false;
}

std::optional<std::array<hal::ScalerHeader, hal::ScalerBuffers::kBuffers>> FrameCopier::
    read_heads_() const noexcept {
    std::array<hal::ScalerHeader, hal::ScalerBuffers::kBuffers> out{};
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
        const auto h = window_->header(base_(i));
        if (!h)
            return std::nullopt;
        out[i] = *h;
    }
    return out;
}

void FrameCopier::poll_triple_(std::int64_t now) noexcept {
    constexpr std::size_t kLiveBuffersForOrder = 2;
    constexpr CoreFrameGap kReorderGap{2};
    constexpr CoreFrameGap kPredecessor{1};
    constexpr std::uint32_t kWovenFields = 2;
    constexpr std::uint32_t kPlainFields = 1;
    const auto heads = read_heads_();
    if (!heads) {
        ++st_.bad_header;
        dirty_ = true;
        return;
    }
    const auto& hd = *heads;
    track_(hd, now);
    std::size_t nlive = 0;
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
        if (!live_[i])
            continue;
        ++nlive;
        if (hd[i].supported())
            continue;
        ++st_.bad_header;
        dirty_ = true;
        return;
    }

    if (live_[0] && !hd[0].triple_buffered())
        return follow_mode_(true, now);
    const auto pick = hal::ScalerProbe::pick_complete(hd, live_);
    if (!pick) {

        if (nlive < kLiveBuffersForOrder)
            return;
        if (gen_.matched) {
            ++st_.unmatched;
            dirty_ = true;
        }
        gen_.matched = false;
        return;
    }
    gen_.matched = true;
    const auto ctr = hd[pick->buf].frame_counter();
    if (numbering_.last() && ctr == numbering_.last()->ctr)
        return;
    gen_.lag_steps = pick->lag;

    if (!pick->woven
        && pick->prev != ScalerBuffers::kBuffers
        && numbering_.last()
        && CoreFrameGap::of_lead(ctr.lead_over(numbering_.last()->ctr)) >= kReorderGap) {
        on_complete_(pick->prev, ctr.minus(hal::HeaderCtr::Lag{1}), now, kPredecessor, 1);

        if (st_.state != RecState::Armed && st_.state != RecState::Recording)
            return;
        if (gen_.regrow_bytes != 0)
            return;
    }

    on_complete_(pick->buf, ctr, now, CoreFrameGap{}, pick->woven ? kWovenFields : kPlainFields);
    if (gen_.woven != pick->woven) {
        gen_.woven = pick->woven;
        st_.interlaced = gen_.woven;
        dirty_ = true;
    }
}

void FrameCopier::track_(
    const std::array<hal::ScalerHeader, hal::ScalerBuffers::kBuffers>& hd,
    std::int64_t now
) noexcept {

    if (now - track_ns_ > numbering_.period())
        for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i)
            watch_[i].seen = std::chrono::nanoseconds{now};
    track_ns_ = now;
    bool odd = false;
    bool parked = false;
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
        const ScalerHeader& h = hd[i];
        if (h != watch_[i].last) {

            const ScalerHeader was = watch_[i].last;

            odd = odd
                || (live_[i]
                    && h.supported()
                    && was.supported()
                    && !h.interlaced()
                    && h.frame_counter().parity() != was.frame_counter().parity());
            watch_[i].last = h;
            watch_[i].seen = std::chrono::nanoseconds{now};

            live_[i] = live_[i] || h.supported();
        } else if (
            std::chrono::nanoseconds{now} - watch_[i].seen
            > kParkPeriods * std::chrono::nanoseconds{numbering_.period()}
        ) {
            live_[i] = false;
        }
        parked = parked || !live_[i];
    }

    if (!odd || !parked)
        return;
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
        if (live_[i] || !hd[i].supported())
            continue;
        live_[i] = true;
        watch_[i].seen = std::chrono::nanoseconds{now};
    }
}

void FrameCopier::track_reset_(const BufMask& live, std::int64_t now) noexcept {
    for (std::size_t i = 0; i < ScalerBuffers::kBuffers; ++i) {
        const auto h = window_->header(base_(i));
        watch_[i].last = h ? *h : ScalerHeader{};
        watch_[i].seen = std::chrono::nanoseconds{now};
        live_[i] = live[i];
    }
    track_ns_ = now;
}

void FrameCopier::poll_lowlat_(std::int64_t now) noexcept {
    const auto h = window_->header(0);
    if (!h || !h->supported()) {
        ++st_.bad_header;
        dirty_ = true;
        return;
    }
    if (h->triple_buffered())
        return follow_mode_(false, now);
    const hal::HeaderCtr c = h->frame_counter();
    const bool fresh = !numbering_.last() || c != numbering_.last()->ctr;
    if (fresh && (!gen_.ll_waiting || c != ll_ctr_)) {

        std::int64_t push = now;
        if (gen_.ll_push_ns != 0) {
            const std::int64_t period_ns = numbering_.period();
            const std::int64_t k = (now - gen_.ll_push_ns + period_ns / 2) / period_ns;
            const std::int64_t predicted = gen_.ll_push_ns + k * period_ns;
            if (predicted <= now && std::chrono::nanoseconds{now - predicted} < kPollMs)
                push = predicted;
        }
        gen_.ll_push_ns = push;
        ll_ctr_ = c;
        gen_.ll_waiting = true;
        ll_due_ns_ = push + numbering_.period() - copy_ns_ - kLowlatGuardNs;
    }
    if (gen_.ll_waiting && now >= ll_due_ns_) {
        gen_.ll_waiting = false;
        on_complete_(0, ll_ctr_, now, CoreFrameGap{}, 1);
    }
}

void FrameCopier::follow_mode_(bool single, std::int64_t now) noexcept {

    if (!single)
        track_reset_(BufMask{true, false, false}, now);
    gen_.lowlat = single;
    st_.lowlat = single;
    gen_.ll_waiting = false;
    gen_.ll_push_ns = 0;
    dirty_ = true;
}

std::int64_t FrameCopier::approach_(std::int64_t now) noexcept {
    if (!gen_.ll_waiting || now >= ll_due_ns_ || ll_due_ns_ - now >= kMsNs)
        return now;
    w_.delay.sleep_until(
        mister::os::RawMonotonicClock::time_point{std::chrono::nanoseconds{ll_due_ns_}}
    );
    return w_.clock.now().time_since_epoch().count();
}

bool FrameCopier::open_armed_(
    const FrameStamp& s,
    infra::OptRef<const CoreFrameRecord> cell,
    std::uint32_t fields,
    std::int64_t now
) noexcept {

    if (!cell || cell->movie_frame < MovieFrame{})
        return false;
    const WideMovieFrame last_field = wide_frame(s.movie_frame) + MovieSpan{fields} - MovieSpan{1};
    if (last_field < WideMovieFrame{})
        return false;
    if (!open_segment_())
        return false;
    const MovieSpan since_zero = gen_.covers_zero ? wide_frame(s.movie_frame) - WideMovieFrame{}
                                                  : MovieSpan{};

    const MovieSpan since_arm{(now - armed_ns_) / std::max<std::int64_t>(numbering_.period(), 1)};
    if (const MovieSpan owed = std::min(since_zero, since_arm); owed > MovieSpan{}) {
        const CoreFrameCount n = owed.core_frames();
        const FrameStamp from = s.rewound(n);
        st_.missed += n;
        st_.first_core_frame = from.core_frame;
        defer_(from.run(n, DupReason::Missed));
    }
    return true;
}

void FrameCopier::on_complete_(
    std::size_t buf,
    hal::HeaderCtr ctr,
    std::int64_t now,
    CoreFrameGap behind,
    std::uint32_t fields
) noexcept {

    CoreFrameRecord rec{};
    const bool have = w_.frames.sample_into(rec) != 0 && rec.supported;
    infra::OptRef<const CoreFrameRecord> cell{};
    if (have) {
        gen_.last_cell = rec;
        cell = infra::OptRef<const CoreFrameRecord>{rec};
    }
    const CoreFrameGap lag = lag_(behind);
    const auto gap = numbering_.frames_since(ctr, now, cell, lag, behind, gen_.woven, st_.anchored);
    const CoreFrameCount n = gap.frames;
    if (gap.estimated)
        ++st_.gap_estimated;
    numbering_.advance(n);

    if (have
        && (!st_.anchored || st_.state == RecState::Armed)
        && st_.rows == CoreFrameCount{}
        && gen_.npending == 0) {
        if (numbering_.anchor(rec, lag, behind == CoreFrameGap{}, st_.anchored)) {
            st_.anchored = true;
            dirty_ = true;
        }
    }
    const auto stamped = numbering_.stamp(ctr, now, cell, gen_.last_cell, st_.anchored, lag);
    if (stamped.rebased)
        ++st_.rebased;
    if (stamped.drifted)
        ++st_.stamp_drift;
    if (stamped.rebased || stamped.drifted)
        dirty_ = true;
    FrameStamp s = stamped.stamp;
    if (st_.state == RecState::Armed) {
        if (!open_armed_(s, cell, fields, now))
            return;
    } else if (n > CoreFrameCount{1}) {

        const CoreFrameCount woven = gen_.woven ? CoreFrameCount{1} : CoreFrameCount{};
        const CoreFrameCount missed = n - CoreFrameCount{1} - woven;
        if (gen_.woven) {

            FrameStampRun field = s.rewound(n - CoreFrameCount{1})
                                      .run(CoreFrameCount{1}, DupReason::Woven);
            const FrameStampRun* last = gen_.npending > 0 ? &pending_[gen_.npending - 1] : nullptr;
            const bool held = last != nullptr && last->precedes(field);
            if (held)
                field.why = last->why;
            if (!held)
                ++st_.woven;
            defer_(field);
        }
        if (missed != CoreFrameCount{}) {
            st_.missed += missed;
            defer_(s.rewound(missed).run(missed, DupReason::Missed));
        }
    }
    if (st_.rows == CoreFrameCount{} && gen_.npending == 0)
        st_.first_core_frame = s.core_frame;
    const auto h = window_->header(base_(buf));
    if (!h || !geometry_ok_(*h)) {
        ++st_.bad_header;
        dirty_ = true;
        return defer_(s.run(CoreFrameCount{1}, DupReason::Missed));
    }
    deliver_(buf, *h, s);
}

bool FrameCopier::geometry_ok_(const ScalerHeader& h) const noexcept {

    if (!h.supported() || !h.consistent())
        return false;
    return h.header_size + h.image_bytes() <= hal::stride_bytes(gen_.stride);
}

void FrameCopier::defer_(const FrameStampRun& run) noexcept {
    if (run.count == CoreFrameCount{})
        return;
    if (gen_.npending > 0) {
        FrameStampRun& last = pending_[gen_.npending - 1];

        if ((last.precedes(run) && last.why == run.why)
            || gen_.npending == RawFrameSlot::kMaxRuns) {
            last.count += run.count;
            return;
        }
    }
    pending_[gen_.npending++] = run;
}

bool FrameCopier::rate_is_ours_(std::uint32_t seq) const noexcept {
    if (gen_.core_seq_known)
        return seq == core_seq_;
    if (w_.frames.generation() == 0)
        return true;
    CoreFrameRecord f{};
    return w_.frames.sample_into(f) != 0 && seq == f.core_seq;
}

void FrameCopier::deliver_(
    std::size_t buf,
    const ScalerHeader& h,
    const FrameStamp& stamp
) noexcept {
    constexpr std::int64_t kSmoothOld = 3;
    constexpr std::int64_t kSmoothSum = 4;
    auto loan = w_.channel.acquire();
    if (!loan) {
        ++st_.backpressure;
        dirty_ = true;
        return defer_(stamp.run(CoreFrameCount{1}, DupReason::Backpressure));
    }
    FrameStamp s = stamp;
    RawFrameSlot::Frame frame{};
    frame.runs = gen_.npending;
    frame.depth = st_.depth;
    CoreFrameCount rows{1};
    for (std::uint8_t i = 0; i < gen_.npending; ++i) {
        frame.run[i] = pending_[i];
        rows += pending_[i].count;
    }
    gen_.npending = 0;
    frame.width = PicturePixels::of(h.width);
    frame.rgb.rows = PictureRows{h.height};
    frame.rgb.stride = RowBytes{h.line};
    VideoGeometryRecord g{};
    if (w_.video.sample_into(g) != 0 && g.valid && rate_is_ours_(g.core_seq))
        frame.rate = g.rate;
    const std::size_t bytes = h.image_bytes();
    const std::span<std::byte> stripe = arena_.stripe(*w_.channel.index_of(loan));
    if (bytes > stripe.size()) {
        s.dup = DupReason::Resize;
        ++st_.resize;
        gen_.regrow_bytes = std::max(gen_.regrow_bytes, bytes);
    } else {
        const auto t0 = w_.clock.now();
        const auto r = window_->copy(base_(buf) + h.header_size, stripe.first(bytes));
        const std::chrono::nanoseconds took = w_.clock.now() - t0;
        const auto after = window_->header(base_(buf));
        copy_ns_ = (kSmoothOld * copy_ns_ + took.count()) / kSmoothSum;
        st_.copy_us.note(us_of(took));

        if (!r || !after || after->frame_counter() != s.header_ctr) {
            s.dup = DupReason::Torn;
            ++st_.torn;
        } else {
            ++st_.captured;
            frame.rgb.bytes = stripe.first(bytes);
        }
    }
    frame.stamp = s;
    *loan = infra::make<RawFrameSlot>(frame, st_.gen);
    w_.channel.send(std::move(loan));
    st_.rows += rows;
    st_.last_core_frame = s.core_frame;
    st_.width = h.width;
    st_.height = h.height;
    dirty_ = true;
    if (gen_.tail_counting) {
        gen_.tail_left = rows >= gen_.tail_left ? CoreFrameCount{} : gen_.tail_left - rows;
        if (gen_.tail_left == CoreFrameCount{})
            end_(RecVerdict::ReplayEnded);
    }
}

}
