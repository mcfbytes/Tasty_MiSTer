// SPDX-License-Identifier: GPL-3.0-or-later
#include "diag_main.h"

#include <chrono>

namespace mister::fw {

void DiagMain::start() noexcept {
    TASTY_SEAT_BODY(DiagMain);
    loop_();
}

void DiagMain::serve() noexcept {
    const std::chrono::milliseconds period = sampler_.sample_period();
    if (slice_ == 0 || stopping()) {
        sampler_.sample(stopping());
        if (stopping()) {
            final_sampled_ = true;
            return;
        }
        if (period == std::chrono::milliseconds::zero()) {
            rest_ms_ = infra::kNoWait;
            return;
        }
    }
    sampler_.answer();
    const std::chrono::milliseconds slept = kDiagSlice * slice_;
    const std::chrono::milliseconds left = period > slept ? period - slept
                                                          : std::chrono::milliseconds::zero();
    rest_ms_ = std::chrono::duration_cast<infra::ParkTimeout>(
        left < kDiagSlice ? left : kDiagSlice
    );
    slice_ = slept + kDiagSlice >= period ? 0u : slice_ + 1u;
}

void DiagMain::settle() noexcept {
    if (!final_sampled_)
        sampler_.sample(true);
    sampler_.render_final();
}

}
