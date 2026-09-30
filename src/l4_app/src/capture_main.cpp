// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/capture_main.h"

namespace mister::app {

Ex<void> CaptureMain::open() noexcept {
    if (wake_.fd() < 0) {
        if (auto r = wake_.open_fd(); !r) return r;
    }
    if (stop_.fd() < 0) {
        if (auto r = stop_.open_fd(); !r) return r;
    }
    if (!park_) park_.emplace(wake_, stop_);
    return {};
}

void CaptureMain::start() noexcept {
    TASTY_SEAT_BODY(CaptureMain);
    if (!park_) {
        if constexpr (kSeatChecksEnabled)
            fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "CaptureMain started unopened");
        return;
    }
    loop_();
}

void CaptureMain::serve() noexcept {
    if (stopping()) return;
    copier_.serve();
}

}  // namespace mister::app
