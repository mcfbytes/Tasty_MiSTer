// SPDX-License-Identifier: GPL-3.0-or-later
#include "diag_main.h"

namespace mister::fw {

void DiagMain::start() noexcept {
    TASTY_SEAT_BODY(DiagMain);
    loop_();
}

void DiagMain::serve() noexcept {
    const unsigned period = sampler_.sample_period_ms();
    if (slice_ == 0 || stopping()) {
        sampler_.sample(stopping());
        if (stopping()) {
            final_sampled_ = true;
            return;
        }
        if (period == 0) {
            rest_ms_ = 0;
            return;
        }
    }
    sampler_.answer();
    const unsigned slept = slice_ * kDiagSliceMs;
    const unsigned left = period > slept ? period - slept : 0u;
    rest_ms_ = left < kDiagSliceMs ? left : kDiagSliceMs;
    slice_ = slept + kDiagSliceMs >= period ? 0u : slice_ + 1u;
}

void DiagMain::settle() noexcept {
    if (!final_sampled_) sampler_.sample(true);
    sampler_.render_final();
}

}  // namespace mister::fw
