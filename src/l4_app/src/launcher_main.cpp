// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/launcher_main.h"

namespace mister::app {

Ex<void> LauncherMain::open() noexcept {
    if (auto r = host_.open(); !r) return r;
    if (wake_.fd() < 0) {
        if (auto r = wake_.open_fd(); !r) return r;
    }
    if (stop_.fd() < 0) {
        if (auto r = stop_.open_fd(); !r) return r;
    }
    if (!park_) park_.emplace(wake_, stop_, host_.poll_fd());
    return {};
}

void LauncherMain::start() noexcept {
    TASTY_SEAT_BODY(LauncherMain);
    if (!park_) {
        if constexpr (kSeatChecksEnabled)
            fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "LauncherMain started unopened");
        return;
    }
    loop_();
}

}  // namespace mister::app
