// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_numbering.h"

#include <algorithm>

#include "frame_ctr_detail.h"

namespace mister::app {

void FrameNumbering::restart() noexcept {
    last_.reset();
    ext_ = 0;
    anchor_off_ = -1;
    cell_off_ = 0;
    cell_epoch_ = 0;
    nvotes_ = 0;
}

std::uint64_t FrameNumbering::core_frame() const noexcept {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(ext_) + anchor_off_);
}

std::int32_t FrameNumbering::movie_of(
    const CoreFrameRecord& cell,
    std::uint64_t core
) const noexcept {
    if (cell.movie_frame == -1)
        return -1;
    const std::int64_t m = static_cast<std::int64_t>(cell.movie_frame)
        + (static_cast<std::int64_t>(core)
           + cell_off_
           - static_cast<std::int64_t>(cell.core_frame));
    return static_cast<std::int32_t>(std::max<std::int64_t>(m, -0x7FFF'FFFF));
}

bool FrameNumbering::anchor(
    const CoreFrameRecord& cell,
    std::int64_t lag,
    bool vote,
    bool anchored
) noexcept {

    const std::int64_t off = static_cast<std::int64_t>(cell.core_frame)
        - lag
        - static_cast<std::int64_t>(ext_);

    if (!anchored || cell.epoch != cell_epoch_ || off > anchor_off_ + 1 || off < anchor_off_ - 1)
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
    cell_off_ = 0;
    cell_epoch_ = cell.epoch;
    return true;
}

void FrameNumbering::rebase_(
    const CoreFrameRecord& cell,
    std::uint64_t core,
    std::int64_t lag
) noexcept {

    cell_off_ = static_cast<std::int64_t>(cell.core_frame) - lag - static_cast<std::int64_t>(core);
    cell_epoch_ = cell.epoch;
}

bool FrameNumbering::cross_check_(
    const CoreFrameRecord& cell,
    std::uint64_t core,
    std::int64_t lag
) const noexcept {
    const std::int64_t d = static_cast<std::int64_t>(cell.core_frame)
        - lag
        - (static_cast<std::int64_t>(core) + cell_off_);
    return d > 1 || d < -1;
}

FrameNumbering::Gap FrameNumbering::frames_since(
    std::uint8_t ctr,
    std::int64_t now,
    const CoreFrameRecord* cell,
    std::int64_t lag,
    std::int64_t behind,
    bool woven,
    bool anchored,
    std::int64_t& period_ns
) noexcept {
    if (!last_) {
        last_ = LastCtr{ctr, now};
        return {1, false};
    }
    const std::uint32_t d = ctr_minus(ctr, last_->ctr);
    const std::int64_t dt = now - last_->seen_ns;
    std::uint32_t n = d;
    bool estimated = false;
    const std::int64_t est = (dt + period_ns / 2) / period_ns;
    if (est >= 7) {

        n = d + 8u * static_cast<std::uint32_t>((est - d + 4) / 8);
        if (anchored && cell != nullptr && cell->epoch == cell_epoch_) {
            const std::int64_t last = static_cast<std::int64_t>(ext_) + anchor_off_ + cell_off_;
            const std::int64_t by_cell = static_cast<std::int64_t>(cell->core_frame) - lag - last;

            if (by_cell + 1 >= est)
                n = d
                    + 8u
                        * static_cast<std::uint32_t>(
                            std::max<std::int64_t>(0, (by_cell - std::int64_t{d} + 4) / 8)
                        );
        }
        estimated = true;
        nvotes_ = vote_next_ = 0;
    } else if (
        const std::uint32_t step = woven ? 2u : 1u;
        d == step && behind == 0 && dt > 0 && dt < std::int64_t{step + 1} * period_ns
    ) {

        period_ns = (7 * period_ns + dt / step) / 8;
    }
    last_ = LastCtr{ctr, now};
    return {n, estimated};
}

FrameNumbering::Stamped FrameNumbering::stamp(
    std::uint8_t ctr,
    std::int64_t now,
    const CoreFrameRecord* cell,
    const std::optional<CoreFrameRecord>& last_cell,
    bool anchored,
    std::int64_t lag
) noexcept {
    Stamped out{};
    FrameStamp& s = out.stamp;
    s.header_ctr = ctr;
    s.capture_ns = now;
    const std::uint64_t core = core_frame();
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
