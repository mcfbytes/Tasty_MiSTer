// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_numbering.h"

#include <algorithm>
#include <limits>

#include "frame_ctr_detail.h"

namespace mister::app {

namespace {

constexpr std::int64_t kPeriodWeight = 7;
constexpr std::int64_t kPeriodSamples = kPeriodWeight + 1;

constexpr CoreFrameGap kWovenStep{2};

}

void FrameNumbering::restart() noexcept {
    last_.reset();
    ext_ = WideCoreFrame{};
    anchor_off_ = kBeforeFirstCopy;
    cell_off_ = CoreFrameGap{};
    cell_epoch_ = 0;
    nvotes_ = 0;
}

MovieFrame FrameNumbering::movie_of(
    const CoreFrameRecord& cell,
    WideCoreFrame core
) const noexcept {
    if (cell.movie_frame == kNoMovieFrame)
        return kNoMovieFrame;
    const CoreFrameGap since_cell = core + cell_off_ - WideCoreFrame::numbered(cell.core_frame);
    const WideMovieFrame m = wide_frame(cell.movie_frame) + since_cell.movie_span();
    constexpr WideMovieFrame kFloor{-std::numeric_limits<std::int32_t>::max()};
    return std::max(m, kFloor).narrowed();
}

bool FrameNumbering::anchor(
    const CoreFrameRecord& cell,
    CoreFrameGap lag,
    bool vote,
    bool anchored
) noexcept {

    const CoreFrameGap off = WideCoreFrame::numbered(cell.core_frame) - lag - ext_;

    if (!anchored || cell.epoch != cell_epoch_ || (off - anchor_off_).beyond(kOneFrame))
        nvotes_ = vote_next_ = 0;
    if (!vote && nvotes_ != 0)
        return false;
    votes_[vote_next_] = off;
    vote_next_ = static_cast<std::uint8_t>((vote_next_ + 1u) % kAnchorVotes);
    if (nvotes_ < kAnchorVotes)
        ++nvotes_;
    const auto first = votes_.begin();
    const auto last = first + nvotes_;
    std::ptrdiff_t best = 0;
    for (auto v = first; v != last; ++v) {
        const std::ptrdiff_t n = std::count(first, last, *v);
        if (n > best || (n == best && *v > anchor_off_)) {
            best = n;
            anchor_off_ = *v;
        }
    }
    cell_off_ = CoreFrameGap{};
    cell_epoch_ = cell.epoch;
    return true;
}

void FrameNumbering::rebase_(
    const CoreFrameRecord& cell,
    WideCoreFrame core,
    CoreFrameGap lag
) noexcept {

    cell_off_ = WideCoreFrame::numbered(cell.core_frame) - lag - core;
    cell_epoch_ = cell.epoch;
}

bool FrameNumbering::cross_check_(
    const CoreFrameRecord& cell,
    WideCoreFrame core,
    CoreFrameGap lag
) const noexcept {
    const WideCoreFrame by_cell = WideCoreFrame::numbered(cell.core_frame) - lag;
    const WideCoreFrame ours = core + cell_off_;
    return (by_cell - ours).beyond(kOneFrame);
}

FrameNumbering::Gap FrameNumbering::frames_since(
    hal::HeaderCtr ctr,
    std::int64_t now,
    const CoreFrameRecord* cell,
    CoreFrameGap lag,
    CoreFrameGap behind,
    bool woven,
    bool anchored,
    std::int64_t& period_ns
) noexcept {
    if (!last_) {
        last_ = LastCtr{ctr, now};
        return {CoreFrameCount{1}, false};
    }
    const CoreFrameGap d = ctr_distance(last_->ctr, ctr);
    const std::int64_t dt = now - last_->seen_ns;
    CoreFrameGap n = d;
    bool estimated = false;
    const CoreFrameGap est{(dt + period_ns / 2) / period_ns};
    if (est >= kCtrReach) {

        n = unwrapped_near(d, est);
        if (anchored && cell != nullptr && cell->epoch == cell_epoch_) {
            const WideCoreFrame last = current_() + cell_off_;
            const CoreFrameGap by_cell = WideCoreFrame::numbered(cell->core_frame) - lag - last;

            if (by_cell + kOneFrame >= est)
                n = unwrapped_near(d, by_cell);
        }
        estimated = true;
        nvotes_ = vote_next_ = 0;
    } else if (
        const CoreFrameGap step = woven ? kWovenStep : kOneFrame; d == step
        && behind == CoreFrameGap{}
        && dt > 0
        && dt < (step + kOneFrame).lasting_ns(period_ns)
    ) {

        period_ns = (kPeriodWeight * period_ns + step.period_ns_of(dt)) / kPeriodSamples;
    }
    last_ = LastCtr{ctr, now};
    return {n.core_frames(), estimated};
}

FrameNumbering::Stamped FrameNumbering::stamp(
    hal::HeaderCtr ctr,
    std::int64_t now,
    const CoreFrameRecord* cell,
    const std::optional<CoreFrameRecord>& last_cell,
    bool anchored,
    CoreFrameGap lag
) noexcept {
    Stamped out{};
    FrameStamp& s = out.stamp;
    s.header_ctr = ctr;
    s.capture_ns = now;
    const WideCoreFrame core = current_();
    s.core_frame = core;

    if (cell == nullptr && anchored && last_cell && last_cell->epoch == cell_epoch_)
        s.movie_frame = movie_of(*last_cell, core);
    if (cell == nullptr || !anchored)
        return out;
    if (cell->epoch != cell_epoch_) {
        rebase_(*cell, core, lag);
        out.rebased = true;
    }
    out.drifted = cross_check_(*cell, core, lag);
    s.movie_frame = movie_of(*cell, core);
    return out;
}

}
