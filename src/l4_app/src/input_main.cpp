// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/input_main.h"

namespace mister::app {

Ex<void> InputMain::open() noexcept {
    if (opened_) return {};
    if (!decode_.ready()) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (stop_.fd() < 0) {
        if (auto r = stop_.open_fd(); !r) return r;
    }
    if (auto w = decode_.watch_stop(stop_); !w) return w;
    if (pause_wake_.fd() < 0) {
        if (auto r = pause_wake_.open_fd(); !r) return r;
    }
    if (auto w = decode_.watch_wake(pause_wake_); !w) return w;
    opened_ = true;
    return {};
}

void InputMain::start() noexcept {
    TASTY_SEAT_BODY(InputMain);
    if (!opened_) {
        if constexpr (kSeatChecksEnabled)
            fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "InputMain started unopened");
        return;
    }
    loop_();
}

void InputMain::serve() noexcept {
    (void)decode_.after_wait();
    if (stopping()) return;
    decode_.before_wait();
}

void InputMain::on_pause() noexcept {
    decode_.mute(true);
    decode_.end_ps2_control();
    paused_link_drops_ += decode_.drop_link_events();
}

}  // namespace mister::app
