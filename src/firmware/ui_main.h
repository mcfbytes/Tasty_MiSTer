// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/uart_mode.h"
#include "infra/error.h"
#include "infra/park_fds.h"
#include "infra/pause_latch.h"
#include "infra/seat.h"
#include "infra/seat_main.h"
#include "infra/seat_park.h"
#include "infra/wake_flag.h"
#include "hal/thread_map.h"

namespace mister::app {
class BtPump;
class CmdFifo;
class EventQueue;
class IEventSink;
class IOwnerTick;
class IUiRoundPort;
class LauncherScreen;
class MglPump;
class LinkRxChannel;
class RecorderControl;
class ReplayFeeder;
class ScreenshotPump;
class VideoPump;
struct Event;
}  // namespace mister::app

namespace mister::fw {

inline constexpr int kUiPollMs = 10;

class UiMain final : public xthread::SeatMain<UiMain> {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr SeatTag kSeat = SeatTag::Ui;

    struct Wires {
        xthread::WakeFlag& wake;
        app::UartModeController::Handoffs& uart_handoffs;
    };

    struct Wiring {
        app::CmdFifo* fifo = nullptr;
        app::MglPump* mgl = nullptr;
        app::VideoPump& video;
        app::UartModeController* uart = nullptr;
        app::IEventSink& ui_sink;
        app::EventQueue& owner_events;
        app::BtPump* bt = nullptr;
        app::ScreenshotPump& shots;
        app::ReplayFeeder& replay;
        app::RecorderControl& recorder;
        app::LauncherScreen* launcher_screen = nullptr;
        app::IUiRoundPort* ui_round = nullptr;
        app::LinkRxChannel& link_rx;
    };

    UiMain(app::EventQueue& events, xthread::ParkFds fds,
           app::UartModeController::Handoffs& handoffs, const Wiring& wiring,
           xthread::WakeFlag& asker) noexcept;
    UiMain(const UiMain&) = delete;
    UiMain& operator=(const UiMain&) = delete;
    UiMain(UiMain&&) = delete;
    UiMain& operator=(UiMain&&) = delete;

    void set_owner_tick(app::IOwnerTick* tick) noexcept { owner_tick_ = tick; }

    void start() noexcept;
    void stop() noexcept { stop_.request(); }

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept { return uart_handoffs_.empty(); }
    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }
    [[nodiscard]] int park_ms() const noexcept { return kUiPollMs; }
    [[nodiscard]] xthread::SeatPark& park() noexcept { return park_; }
    [[nodiscard]] bool paused() noexcept { return pause_.observe(*this); }
    [[nodiscard]] bool pause_pending() const noexcept { return pause_.pending(); }
    void settle() noexcept;

    void on_pause() noexcept;
    void on_resume() noexcept;
    [[nodiscard]] bool quiescent() const noexcept { return true; }

    [[nodiscard]] xthread::PauseLatch& pause_latch() noexcept { return pause_; }

    [[nodiscard]] std::uint32_t paused_link_drops() const noexcept { return paused_link_drops_; }

    [[nodiscard]] std::uint32_t roundless_link_drops() const noexcept {
        return roundless_link_drops_;
    }

private:
    void drain_events_() noexcept;

    app::EventQueue& events_;
    app::EventQueue& owner_events_;
    app::CmdFifo* const fifo_;
    app::MglPump* const mgl_;
    app::VideoPump& video_;
    app::BtPump* const bt_;
    app::ScreenshotPump& shots_;
    app::ReplayFeeder& replay_;
    app::RecorderControl& recorder_;
    app::LauncherScreen* const launcher_screen_;
    app::UartModeController* const uart_;
    app::IEventSink& ui_sink_;
    app::IUiRoundPort* const ui_round_;
    app::IOwnerTick* owner_tick_ = nullptr;
    app::LinkRxChannel& ui_rx_;
    xthread::WakeFlag& wake_;
    xthread::WakeFlag stop_;
    xthread::PauseLatch pause_;
    std::uint32_t paused_link_drops_ = 0;
    std::uint32_t roundless_link_drops_ = 0;

    app::UartModeController::Handoffs& uart_handoffs_;
    xthread::SeatPark park_;
};

static_assert(xthread::SeatBody<UiMain>);

}  // namespace mister::fw
