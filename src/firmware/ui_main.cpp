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

UiMain::UiMain(app::EventQueue& events, xthread::ParkFds fds,
               app::UartModeController::Handoffs& handoffs, const Wiring& wiring,
               xthread::WakeFlag& asker) noexcept
    : events_(events), owner_events_(wiring.owner_events), fifo_(wiring.fifo), mgl_(wiring.mgl),
      video_(wiring.video), bt_(wiring.bt), shots_(wiring.shots), replay_(wiring.replay),
      recorder_(wiring.recorder), launcher_screen_(wiring.launcher_screen), uart_(wiring.uart),
      ui_sink_(wiring.ui_sink), ui_round_(wiring.ui_round), ui_rx_(wiring.link_rx),
      wake_(fds.wake()), stop_(std::move(fds).take_stop()), pause_(wake_, &asker),
      uart_handoffs_(handoffs), park_(wake_, stop_, fifo_ != nullptr ? fifo_->fd() : -1) {}

void UiMain::start() noexcept {
    TASTY_SEAT_BODY(UiMain);
    if (ui_round_ != nullptr) ui_round_->boot_witness();
    loop_();
}

void UiMain::drain_events_() noexcept {
    while (auto e = events_.take())
        ui_sink_.deliver(*e);
    while (auto e = owner_events_.take())
        ui_sink_.deliver(*e);
}

void UiMain::serve() noexcept {
    bool link_work = false;

    while (const auto ev = ui_rx_.pop()) {
        if (ui_round_ != nullptr)
            link_work |= ui_round_->take_link(*ev);
        else
            ++roundless_link_drops_;
    }
    if (fifo_ != nullptr && park_.extra_ready()) (void)fifo_->service();
    if (mgl_ != nullptr) mgl_->tick();
    video_.tick();
    if (launcher_screen_ != nullptr) launcher_screen_->tick();
    if (bt_ != nullptr) bt_->tick();
    shots_.tick();
    replay_.tick();
    recorder_.tick();
    if (owner_tick_ != nullptr) owner_tick_->tick();
    if (uart_ != nullptr) {
        while (const auto h = uart_handoffs_.pop())
            infra::dispatch<app::UartHandoffRoutes>(*h, *uart_);
    }
    if (ui_round_ != nullptr) ui_round_->round(link_work);
    drain_events_();
}

void UiMain::on_pause() noexcept {
    park_.mute_extra(true);
    while (ui_rx_.pop().has_value())
        ++paused_link_drops_;
    if (ui_round_ != nullptr) ui_round_->forget_cells();
    video_.on_pause();
}

void UiMain::on_resume() noexcept {
    park_.mute_extra(false);
    drain_events_();
}

void UiMain::settle() noexcept {
    drain_events_();
    shots_.tick();
}

}  // namespace mister::fw
