// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/rec_write_main.h"

#include <algorithm>

namespace mister::app {

Ex<void> RecWriteMain::open() noexcept {
    if (wake_.fd() < 0) {
        if (auto r = wake_.open_fd(); !r) return r;
    }
    if (stop_.fd() < 0) {
        if (auto r = stop_.open_fd(); !r) return r;
    }
    if (!park_) park_.emplace(wake_, stop_);
    return {};
}

void RecWriteMain::start() noexcept {
    TASTY_SEAT_BODY(RecWriteMain);
    if (!park_) {
        if constexpr (kSeatChecksEnabled)
            fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "RecWriteMain started unopened");
        return;
    }
    loop_();
}

void RecWriteMain::serve() noexcept {
    avi_.serve();
    writer_.serve();
}

int RecWriteMain::park_ms() const noexcept {
    const int a = avi_.park_ms();
    const int b = writer_.park_ms();
    if (a < 0) return b;
    return b < 0 ? a : std::min(a, b);
}

}  // namespace mister::app
