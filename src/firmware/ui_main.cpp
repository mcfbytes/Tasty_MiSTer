// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_main.h"

#include "app/bt_pump.h"
#include "app/cmd_fifo.h"
#include "app/event.h"
#include "app/event_sink.h"
#include "app/link_rx_channel.h"
#include "app/lw_osd_endpoint.h"
#include "app/mgl_pump.h"
#include "app/launcher_screen.h"
#include "app/owner_tick.h"
#include "app/recorder_control.h"
#include "app/replay_feeder.h"
#include "app/save_state_attach.h"
#include "app/screenshot_pump.h"
#include "app/uart_handoff_dispatch.h"
#include "app/uart_mode.h"
#include "app/ui_round_port.h"
#include "app/video_pump.h"
#include "infra/message_sum.h"

#include <optional>

namespace mister::fw {

UiMain::UiMain(
    app::EventQueue<SeatTag::RT>& events,
    xthread::ParkFds fds,
    app::UartModeController::Handoffs& handoffs,
    const Wiring& wiring,
    xthread::WakeFlag& asker
) noexcept
    : events_(events),
      owner_events_(wiring.owner_events),
      fifo_(wiring.fifo),
      mgl_(wiring.mgl),
      video_(wiring.video),
      bt_(wiring.bt),
      shots_(wiring.shots),
      replay_(wiring.replay),
      recorder_(wiring.recorder),
      launcher_screen_(wiring.launcher_screen),
      uart_(wiring.uart),
      ui_sink_(wiring.ui_sink),
      ui_round_(wiring.ui_round),
      owner_tick_(wiring.owner_tick),
      lw_osd_(wiring.lw_osd),
      ui_rx_(wiring.link_rx),
      save_states_(wiring.save_states),
      wake_(fds.wake()),
      stop_(std::move(fds).take_stop()),
      pause_(wake_, infra::OptRef<xthread::WakeFlag>{asker}),
      uart_handoffs_(handoffs),
      park_(wake_, stop_, fifo_ ? fifo_->fd() : -1) {}

void UiMain::start() noexcept {
    TASTY_SEAT_BODY(UiMain);
    if (ui_round_)
        ui_round_->boot_witness();
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
        if (ui_round_)
            link_work |= ui_round_->take_link(*ev);
        else
            ++roundless_link_drops_;
    }
    if (fifo_ && park_.extra_ready())
        (void)fifo_->service();
    if (mgl_)
        mgl_->tick();
    video_.tick();
    if (launcher_screen_)
        launcher_screen_->tick();
    if (bt_)
        bt_->tick();
    shots_.tick();
    replay_.tick();
    recorder_.tick();
    if (save_states_)
        save_states_->tick();
    if (owner_tick_)
        owner_tick_->tick();
    if (uart_) {
        while (const auto h = uart_handoffs_.pop())
            infra::dispatch<app::UartHandoffRoutes>(*h, uart_performer_);
    }
    const std::optional<app::UiRoundEdge> edge = ui_round_
        ? std::optional{ui_round_->round(link_work)}
        : std::nullopt;

    if (lw_osd_)
        lw_osd_->serve();
    if (edge)
        ui_round_->publish_edges(*edge);
    drain_events_();
}

void UiMain::on_pause() noexcept {
    park_.mute_extra(true);
    while (ui_rx_.pop().has_value())
        ++paused_link_drops_;
    if (ui_round_)
        ui_round_->forget_cells();
    if (save_states_)
        save_states_->on_ui_pause();
    video_.on_pause();
    if (lw_osd_)
        lw_osd_->on_pause();
}

void UiMain::on_resume() noexcept {
    park_.mute_extra(false);
    if (lw_osd_)
        lw_osd_->on_resume();
    drain_events_();
}

void UiMain::settle() noexcept {
    drain_events_();
    shots_.tick();
    if (ui_round_)
        ui_round_->settle();
    if (lw_osd_)
        lw_osd_->on_stop();
}

}
