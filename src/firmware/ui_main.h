// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/uart_mode.h"
#include "infra/error.h"
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
    static constexpr hal::Seat kSeat = hal::Seat::Ui;

    struct Wiring {
        app::CmdFifo* fifo = nullptr;
        app::MglPump* mgl = nullptr;
        app::VideoPump* video = nullptr;
    };

    UiMain(app::EventQueue& events, const Wiring& wiring) noexcept;
    UiMain(const UiMain&) = delete;
    UiMain& operator=(const UiMain&) = delete;
    UiMain(UiMain&&) = delete;
    UiMain& operator=(UiMain&&) = delete;

    void set_ui_sink(app::IEventSink* sink) noexcept { ui_sink_ = sink; }

    void set_owner_events(app::EventQueue* events) noexcept { owner_events_ = events; }
    void set_bt_pump(app::BtPump* pump) noexcept { bt_ = pump; }
    void set_screenshot_pump(app::ScreenshotPump* pump) noexcept { shots_ = pump; }

    void set_replay(app::ReplayFeeder* feeder) noexcept { replay_ = feeder; }

    void set_recorder(app::RecorderControl* rec) noexcept { recorder_ = rec; }

    void set_uart(app::UartModeController* uart) noexcept {
        uart_ = uart;
        if (uart != nullptr) uart->bind_handoffs(uart_handoffs_);
    }

    void set_ui_round(app::IUiRoundPort* round) noexcept { ui_round_ = round; }
    void set_owner_tick(app::IOwnerTick* tick) noexcept { owner_tick_ = tick; }
    void set_link_rx(app::LinkRxChannel* rx) noexcept { ui_rx_ = rx; }

    [[nodiscard]] Ex<void> open() noexcept;

    void start() noexcept;
    void stop() noexcept { stop_.request(); }

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept { return uart_handoffs_.empty(); }
    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }
    [[nodiscard]] int park_ms() const noexcept { return kUiPollMs; }
    [[nodiscard]] xthread::SeatPark& park() noexcept { return *park_; }
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

    app::EventQueue* events_;
    app::EventQueue* owner_events_ = nullptr;
    app::CmdFifo* const fifo_;
    app::MglPump* const mgl_;
    app::VideoPump* const video_;
    app::BtPump* bt_ = nullptr;
    app::ScreenshotPump* shots_ = nullptr;
    app::ReplayFeeder* replay_ = nullptr;
    app::RecorderControl* recorder_ = nullptr;
    app::UartModeController* uart_ = nullptr;
    app::IEventSink* ui_sink_ = nullptr;
    app::IUiRoundPort* ui_round_ = nullptr;
    app::IOwnerTick* owner_tick_ = nullptr;
    app::LinkRxChannel* ui_rx_ = nullptr;
    xthread::WakeFlag wake_{};
    xthread::WakeFlag stop_{};
    xthread::PauseLatch pause_{wake_};
    std::uint32_t paused_link_drops_ = 0;
    std::uint32_t roundless_link_drops_ = 0;

    app::UartModeController::Handoffs uart_handoffs_{wake_};
    std::optional<xthread::SeatPark> park_{};
};

static_assert(xthread::SeatBody<UiMain>);

}  // namespace mister::fw
