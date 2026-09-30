// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/prefetch_main.h"

namespace mister::svc {

Ex<void> PrefetchMain::open() noexcept {
    if (prefetch_.wake().fd() < 0) return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    if (stop_.fd() < 0) {
        if (auto r = stop_.open_fd(); !r) return r;
    }
    if (!park_) park_.emplace(prefetch_.wake(), stop_);
    return {};
}

void PrefetchMain::start() noexcept {
    TASTY_SEAT_BODY(PrefetchMain);
    if (!park_) {
        if constexpr (kSeatChecksEnabled)
            fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "PrefetchMain started unopened");
        return;
    }
    loop_();
}

void PrefetchMain::serve() noexcept {
    if (stopping()) return;
    prefetch_.serve();
}

}  // namespace mister::svc
