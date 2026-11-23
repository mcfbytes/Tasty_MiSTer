// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_main.h"

#include "infra/wired_end.h"
#include "app/cmd_fifo.h"
#include "app/event.h"
#include "app/event_sink.h"
#include "app/game_id_pump.h"
#include "app/link_rx_channel.h"
#include "app/lw_osd_endpoint.h"
#include "app/mgl_pump.h"
#include "app/launcher/launcher_screen.h"
#include "app/recorder_control.h"
#include "app/replay/replay_feeder.h"
#include "app/save_state_attach.h"
#include "app/screenshot_pump.h"
#include "app/uart_handoff_dispatch.h"
#include "app/uart_mode.h"
#include "app/save_state_ui_ticker.h"
#include "app/ui_write_ticker.h"
#include "app/ui_round_port.h"
#include "app/recents/ui_write_lane.h"
#include "app/video_pump.h"
#include "infra/xthread/coworker.h"
#include "infra/message_sum.h"
#include "infra/prelude.h"
#include "infra/opt_ref.h"

#include <optional>

namespace mister::fw {

using infra::wired_end;

namespace {

using Body = infra::OptRef<xthread::ICoworker>;

Body slot_of(xthread::ICoworker& body) noexcept {
    return Body{body};
}

template <class T>
Body slot_of(infra::OptRef<T> body) noexcept {
    return body ? Body{*body} : Body{};
}

}

UiMain::UiMain(
    app::EventQueue<SeatTag::RT>& events,
    xthread::ParkFds fds,
    infra::OptRef<app::UartModeController::Handoffs> handoffs,
    const Wiring& wiring,
    xthread::WakeFlag& asker
) noexcept
    : events_(wired_end(events.consumer(), "T-UI: RT event lane consumer minted twice")),
      owner_events_(
          wired_end(wiring.owner_events.consumer(), "T-UI: MAIN event lane consumer minted twice")
      ),
      fifo_(wiring.fifo),
      video_(wiring.video),
      ui_sink_(wiring.ui_sink),
      ui_round_(wiring.ui_round),
      menu_keys_(wiring.menu_keys),
      fan_(video_, wiring.ui_round),
      lw_osd_(wiring.lw_osd),
      ui_rx_(wiring.link_rx),
      wake_(fds.wake()),
      stop_(std::move(fds).take_stop()),
      pause_(wake_, infra::OptRef<xthread::WakeFlag>{asker}),
      uart_handoffs_(handoffs),
      park_(wake_, stop_, fifo_ ? fifo_->fd() : -1) {
    if (wiring.save_states)
        save_state_ticker_.emplace(*wiring.save_states);
    if (wiring.ui_writes)
        ui_write_ticker_.emplace(*wiring.ui_writes, wiring.verdicts);

    Roster::Slots slots{};
    slots[infra::ordinal(Roster::Slot::mgl)] = slot_of(wiring.mgl);
    slots[infra::ordinal(Roster::Slot::video)] = slot_of(wiring.video);
    slots[infra::ordinal(Roster::Slot::launcher)] = slot_of(wiring.launcher_screen);
    slots[infra::ordinal(Roster::Slot::terminal)] = slot_of(wiring.terminal);
    slots[infra::ordinal(Roster::Slot::shots)] = slot_of(wiring.shots);
    slots[infra::ordinal(Roster::Slot::replay)] = slot_of(wiring.replay);
    slots[infra::ordinal(Roster::Slot::recorder)] = slot_of(wiring.recorder);
    slots[infra::ordinal(Roster::Slot::save_state)] = save_state_ticker_ ? Body{*save_state_ticker_}
                                                                         : Body{};
    slots[infra::ordinal(Roster::Slot::card_writes)] = ui_write_ticker_ ? Body{*ui_write_ticker_}
                                                                        : Body{};
    slots[infra::ordinal(Roster::Slot::game_id)] = slot_of(wiring.game_id);
    slots[infra::ordinal(Roster::Slot::owner)] = slot_of(wiring.owner_tick);
    coworkers_ = Roster{slots};
}

void UiMain::Roster::serve() noexcept {
    for (const Body body : slots_)
        if (body)
            body->serve();
}

void UiMain::Roster::on_pause() noexcept {
    for (auto it = slots_.rbegin(); it != slots_.rend(); ++it)
        if (*it)
            (*it)->on_pause();
}

void UiMain::Roster::on_resume() noexcept {
    for (const Body body : slots_)
        if (body)
            body->on_resume();
}

void UiMain::Roster::on_stop() noexcept {
    for (const Body body : slots_)
        if (body)
            body->on_stop();
}

void UiMain::start() noexcept {
    TASTY_SEAT_BODY(UiMain);
    if (ui_round_)
        ui_round_->boot_witness();
    loop_();
}

void UiMain::drain_events_() noexcept {
    while (auto e = events_.pop())
        ui_sink_.deliver_from(*e, SeatTag::RT);
    while (auto e = owner_events_.pop())
        ui_sink_.deliver_from(*e, SeatTag::Main);
}

void UiMain::serve() noexcept {
    bool link_work = false;

    while (const auto ev = ui_rx_.pop())
        link_work |= fan_.take(*ev);
    if (fifo_ && park_.extra_ready())
        (void)fifo_->service();
    coworkers_.serve();
    if (uart_handoffs_) {
        while (const auto h = uart_handoffs_->pop())
            infra::dispatch<app::UartHandoffRoutes>(*h, uart_performer_);
    }

    unsigned popped = 0;
    if (ui_round_ && menu_keys_) {
        while (!kKeyDrainBudget.spent(popped)) {
            const auto e = menu_keys_->pop();
            if (!e)
                break;
            ui_round_->on(*e);
            ++popped;
        }
    }
    const std::optional<app::UiRoundEdge> edge = ui_round_
        ? std::optional{ui_round_->round(app::UiPassWork{link_work, popped > 0})}
        : std::nullopt;

    if (lw_osd_)
        lw_osd_->serve();
    if (edge)
        ui_round_->publish_edges(*edge);
    if (edge)
        video_.note_menu_present(edge->menu_present);
    drain_events_();
}

void UiMain::on_pause() noexcept {
    park_.mute_extra(true);
    while (ui_rx_.pop().has_value())
        ++paused_link_drops_;
    if (ui_round_)
        ui_round_->forget_cells();
    coworkers_.on_pause();
    if (lw_osd_)
        lw_osd_->on_pause();
}

void UiMain::on_resume() noexcept {
    park_.mute_extra(false);
    coworkers_.on_resume();
    if (lw_osd_)
        lw_osd_->on_resume();
    drain_events_();
}

void UiMain::settle() noexcept {
    drain_events_();
    coworkers_.on_stop();
    if (ui_round_)
        ui_round_->settle();
    if (lw_osd_)
        lw_osd_->on_stop();
}

}
