// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/encode_main.h"

namespace mister::app {

Ex<void> EncodeMain::open() noexcept {
    if (wake_.fd() < 0) {
        if (auto r = wake_.open_fd(); !r) return r;
    }
    if (stop_.fd() < 0) {
        if (auto r = stop_.open_fd(); !r) return r;
    }
    if (!park_) park_.emplace(wake_, stop_);
    return {};
}

void EncodeMain::start() noexcept {
    TASTY_SEAT_BODY(EncodeMain);
    if (!park_) {
        if constexpr (kSeatChecksEnabled)
            fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "EncodeMain started unopened");
        return;
    }
    loop_();
}

void EncodeMain::serve() noexcept {
    hasher_.serve();

    if (stopping() && hasher_.holding())
        delay_.sleep_for(std::chrono::milliseconds(FrameHasher::kHeldPollMs));
}

}  // namespace mister::app
