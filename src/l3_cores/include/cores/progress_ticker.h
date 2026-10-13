// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/load_progress.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::cores {

inline constexpr std::uint16_t kProgressSteps = 168;

class ProgressTicker {
    TASTY_SEAT_RESIDENT(RT);

public:
    void bind(ILoadProgress* sink) noexcept { sink_ = infra::opt_ref_of(sink); }

    void begin(std::uint64_t total) noexcept {
        step_ = 0;
        per_step_ = total / kProgressSteps;
        if (per_step_ == 0) per_step_ = 1;
        next_at_ = per_step_;
        if (sink_) sink_->on_load_progress(0, 0);
    }

    void advance(std::uint64_t offset) noexcept {
        if (!sink_ || offset < next_at_ || step_ >= kProgressSteps) return;

        std::uint64_t reached = offset / per_step_;
        if (reached > kProgressSteps) reached = kProgressSteps;
        if (reached <= step_) return;
        step_ = static_cast<std::uint16_t>(reached);
        next_at_ = (reached + 1) * per_step_;
        sink_->on_load_progress(step_, kProgressSteps);
    }

    void finish() noexcept {
        if (sink_) sink_->on_load_progress(0, 0);
    }

private:
    infra::OptRef<ILoadProgress> sink_{};
    std::uint64_t per_step_ = 1;
    std::uint64_t next_at_ = 0;
    std::uint16_t step_ = 0;
};

}  // namespace mister::cores
