// SPDX-License-Identifier: GPL-3.0-or-later
#include "diag_main.h"

namespace mister::fw {

Ex<void> DiagMain::open() noexcept {
    if (wake_.fd() < 0) {
        if (auto r = wake_.open_fd(); !r) return r;
    }
    if (stop_.fd() < 0) {
        if (auto r = stop_.open_fd(); !r) return r;
    }
    if (!park_) park_.emplace(wake_, stop_);
    return {};
}

void DiagMain::start() noexcept {
    TASTY_SEAT_BODY(DiagMain);
    if (!park_) {
        if constexpr (kSeatChecksEnabled)
            fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "DiagMain started unopened");
        return;
    }
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
