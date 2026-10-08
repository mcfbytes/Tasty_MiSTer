// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/input_main.h"

namespace mister::app {

void InputMain::start() noexcept {
    TASTY_SEAT_BODY(InputMain);
    loop_();
}

void InputMain::serve() noexcept {
    (void)decode_.after_wait();
    if (stopping()) return;
    decode_.before_wait(infra::OptRef<const xthread::WakeFlag>{stop_});
}

void InputMain::on_pause() noexcept {
    decode_.mute(true);
    decode_.end_ps2_control();
    paused_link_drops_ += decode_.drop_link_events();
}

}  // namespace mister::app
