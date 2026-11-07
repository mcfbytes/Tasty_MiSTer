// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/load_progress.h"
#include "cores/progress.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores {

inline constexpr std::uint16_t kProgressSteps = 168;

[[nodiscard]] constexpr proto::FileSize progress_step_bytes(proto::FileSize total) noexcept {
    return total.per_step(kProgressSteps);
}

class ProgressTicker {
    TASTY_SEAT_RESIDENT(RT);

public:
    void bind(ILoadProgress* sink) noexcept { sink_ = infra::opt_ref_of(sink); }

    void begin(proto::FileSize total) noexcept {
        step_ = 0;
        per_step_ = progress_step_bytes(total);
        next_at_ = per_step_;
        if (sink_)
            sink_->on_load_progress(Progress{});
    }

    void advance(std::uint64_t offset) noexcept {
        const proto::FileSize at{offset};
        if (!sink_ || at < next_at_ || step_ >= kProgressSteps)
            return;
        std::uint16_t reached = step_;
        while (reached < kProgressSteps && at >= per_step_ * std::uint64_t{reached + 1u})
            ++reached;
        if (reached <= step_)
            return;
        step_ = reached;
        next_at_ = per_step_ * std::uint64_t{reached + 1u};
        sink_->on_load_progress(Progress{.done = step_, .total = kProgressSteps});
    }

    void finish() noexcept {
        if (sink_)
            sink_->on_load_progress(Progress{});
    }

private:
    infra::OptRef<ILoadProgress> sink_{};
    proto::FileSize per_step_{1};
    proto::FileSize next_at_{};
    std::uint16_t step_ = 0;
};

}
