// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/input_main.h"

#include "app/link_event_dispatch.h"

namespace mister::app {

void InputMain::start() noexcept {
    TASTY_SEAT_BODY(InputMain);
    loop_();
}

void InputMain::serve() noexcept {
    if (decode_.begin_batch()) {

        pop_link_events();
        (void)decode_.decode_batch();
    }
    if (stopping())
        return;
    decode_.begin_round();
    pop_link_events();
    decode_.prepare_wait();
}

void InputMain::on_pause() noexcept {
    decode_.mute(true);
    decode_.end_ps2_control();
    drop_link_events();
}

void InputMain::pop_link_events() noexcept {
    while (const auto ev = link_rx_.pop()) {
        infra::dispatch<LinkEventRoutes<EventSink::Input>>(*ev, decode_);
    }
}

void InputMain::drop_link_events() noexcept {
    while (link_rx_.pop().has_value())
        ++paused_link_drops_;
}

}
