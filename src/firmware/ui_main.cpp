// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_main.h"

#include "app/bt_pump.h"
#include "app/cmd_fifo.h"
#include "app/event.h"
#include "app/event_sink.h"
#include "app/link_rx_channel.h"
#include "app/mgl_pump.h"
#include "app/launcher_screen.h"
#include "app/owner_tick.h"
#include "app/recorder_control.h"
#include "app/replay_feeder.h"
#include "app/screenshot_pump.h"
#include "app/uart_handoff_dispatch.h"
#include "app/uart_mode.h"
#include "app/ui_round_port.h"
#include "app/video_pump.h"
#include "infra/message_sum.h"

namespace mister::fw {

UiMain::UiMain(app::EventQueue& events, const Wiring& wiring) noexcept
    : events_(&events), fifo_(wiring.fifo), mgl_(wiring.mgl), video_(wiring.video) {}

Ex<void> UiMain::open() noexcept {
    if (wake_.fd() < 0) {
        if (auto r = wake_.open_fd(); !r) return r;
    }
    if (stop_.fd() < 0) {
        if (auto r = stop_.open_fd(); !r) return r;
    }
    if (!park_) park_.emplace(wake_, stop_, fifo_ != nullptr ? fifo_->fd() : -1);
    return {};
}

void UiMain::start() noexcept {
    TASTY_SEAT_BODY(UiMain);
    if (!park_) {
        if constexpr (kSeatChecksEnabled)
            fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "UiMain started unopened");
        return;
    }
    if (ui_round_ != nullptr) ui_round_->boot_witness();
    loop_();
}

void UiMain::drain_events_() noexcept {
    while (auto e = events_->take()) {
        if (ui_sink_ != nullptr) ui_sink_->deliver(*e);
    }
    if (owner_events_ != nullptr) {
        while (auto e = owner_events_->take()) {
            if (ui_sink_ != nullptr) ui_sink_->deliver(*e);
        }
    }
}

void UiMain::serve() noexcept {
    bool link_work = false;

    if (ui_rx_ != nullptr) {
        while (const auto ev = ui_rx_->pop()) {
            if (ui_round_ != nullptr)
                link_work |= ui_round_->take_link(*ev);
            else
                ++roundless_link_drops_;
        }
    }
    if (fifo_ != nullptr && park_ && park_->extra_ready()) (void)fifo_->service();
    if (mgl_ != nullptr) mgl_->tick();
    if (video_ != nullptr) video_->tick();
    if (launcher_screen_ != nullptr) launcher_screen_->tick();
    if (bt_ != nullptr) bt_->tick();
    if (shots_ != nullptr) shots_->tick();
    if (replay_ != nullptr) replay_->tick();
    if (recorder_ != nullptr) recorder_->tick();
    if (owner_tick_ != nullptr) owner_tick_->tick();
    if (uart_ != nullptr) {
        while (const auto h = uart_handoffs_.pop())
            infra::dispatch<app::UartHandoffRoutes>(*h, *uart_);
    }
    if (ui_round_ != nullptr) ui_round_->round(link_work);
    drain_events_();
}

void UiMain::on_pause() noexcept {
    if (park_) park_->mute_extra(true);
    if (ui_rx_ != nullptr) {
        while (ui_rx_->pop().has_value())
            ++paused_link_drops_;
    }
    if (ui_round_ != nullptr) ui_round_->forget_cells();
    if (video_ != nullptr) video_->on_pause();
}

void UiMain::on_resume() noexcept {
    if (park_) park_->mute_extra(false);
    drain_events_();
}

void UiMain::settle() noexcept {
    drain_events_();
    if (shots_ != nullptr) shots_->tick();
}

}  // namespace mister::fw
