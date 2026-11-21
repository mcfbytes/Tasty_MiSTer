// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "app/event.h"
#include "app/input/raw_key_edge.h"
#include "app/uart_mode.h"
#include "app/uart_performer.h"
#include "app/save_state_ui_ticker.h"
#include "app/ui_write_ticker.h"
#include "app/recents/verdict_sinks.h"
#include "ui_link_fan.h"
#include "infra/xthread/coworker.h"
#include "infra/error.h"
#include "infra/message_sum.h"
#include "infra/opt_ref.h"
#include "infra/xthread/park_fds.h"
#include "infra/park_timeout.h"
#include "infra/xthread/pause_latch.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/seat_main.h"
#include "infra/xthread/seat_park.h"
#include "infra/xthread/wake_flag.h"
#include "hal/thread_map.h"

namespace mister::app {
class CmdFifo;
class IEventSink;
class IUiRoundPort;

namespace launcher {
class LauncherScreen;
}
class LwOsdEndpoint;
class GameIdPump;
class MglPump;
template <SeatTag Seat>
class LinkRxChannel;
class RecorderControl;

namespace replay {
class ReplayFeeder;
}
class SaveStateAttach;
class ScreenshotPump;
class VideoPump;
struct Event;
}

namespace mister::app::recents {
class UiWriteLane;
}

namespace mister::fw {

inline constexpr infra::ParkTimeout kUiPollMs{10};

class UiMain final : public xthread::SeatMain<UiMain> {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr SeatTag kSeat = SeatTag::Ui;

    struct KeyBudget {
        std::uint16_t v = 0;

        [[nodiscard]] constexpr bool spent(unsigned taken) const noexcept { return taken >= v; }

        friend constexpr bool operator==(KeyBudget, KeyBudget) = default;
    };

    static constexpr KeyBudget kKeyDrainBudget{16};

    struct Wires {
        xthread::WakeFlag& wake;
        infra::OptRef<app::UartModeController::Handoffs> uart_handoffs;
    };

    class Roster {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        enum class Slot : std::uint8_t {
            mgl,
            video,
            launcher,
            shots,
            replay,
            recorder,
            save_state,
            card_writes,
            game_id,
            owner,
            kCount,
        };
        using Slots = std::array<infra::OptRef<xthread::ICoworker>, infra::ordinal(Slot::kCount)>;

        constexpr Roster() noexcept = default;

        explicit constexpr Roster(const Slots& slots) noexcept : slots_(slots) {}

        void serve() noexcept;

        void on_pause() noexcept;
        void on_resume() noexcept;
        void on_stop() noexcept;

    private:
        Slots slots_{};
    };

    struct Wiring {
        infra::OptRef<app::CmdFifo> fifo{};
        infra::OptRef<app::MglPump> mgl{};
        app::VideoPump& video;
        app::IEventSink& ui_sink;
        app::EventQueue<SeatTag::Main>& owner_events;
        app::ScreenshotPump& shots;
        app::replay::ReplayFeeder& replay;
        app::RecorderControl& recorder;
        infra::OptRef<app::launcher::LauncherScreen> launcher_screen{};
        infra::OptRef<app::IUiRoundPort> ui_round{};

        infra::OptRef<app::input::MenuKeyInbox> menu_keys{};
        app::LinkRxChannel<SeatTag::Ui>& link_rx;

        infra::OptRef<app::SaveStateAttach> save_states{};

        infra::OptRef<app::recents::UiWriteLane> ui_writes{};

        app::recents::VerdictSinks verdicts{};

        infra::OptRef<app::GameIdPump> game_id{};

        infra::OptRef<xthread::ICoworker> owner_tick{};

        infra::OptRef<app::LwOsdEndpoint> lw_osd{};
    };

    UiMain(
        app::EventQueue<SeatTag::RT>& events,
        xthread::ParkFds fds,
        infra::OptRef<app::UartModeController::Handoffs> handoffs,
        const Wiring& wiring,
        xthread::WakeFlag& asker
    ) noexcept;
    UiMain(const UiMain&) = delete;
    UiMain& operator=(const UiMain&) = delete;
    UiMain(UiMain&&) = delete;
    UiMain& operator=(UiMain&&) = delete;

    void start() noexcept;

    void stop() noexcept { stop_.request(); }

    void serve() noexcept;

    [[nodiscard]] bool idle() const noexcept { return !uart_handoffs_ || uart_handoffs_->empty(); }

    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }

    [[nodiscard]] infra::ParkTimeout park_ms() const noexcept { return kUiPollMs; }

    [[nodiscard]] xthread::SeatPark& park() noexcept { return park_; }

    [[nodiscard]] bool paused() noexcept { return pause_.observe(pause_seen_, *this); }

    [[nodiscard]] bool pause_pending() const noexcept { return pause_.pending(); }

    void settle() noexcept;

    void on_pause() noexcept;
    void on_resume() noexcept;

    [[nodiscard]] bool quiescent() const noexcept { return true; }

    [[nodiscard]] xthread::PauseLatch& pause_latch() noexcept { return pause_; }

    [[nodiscard]] std::uint32_t paused_link_drops() const noexcept { return paused_link_drops_; }

    [[nodiscard]] std::uint32_t roundless_link_drops() const noexcept {
        return fan_.roundless_drops();
    }

private:
    void drain_events_() noexcept;

    app::EventQueue<SeatTag::RT>::Consumer events_;
    app::EventQueue<SeatTag::Main>::Consumer owner_events_;
    const infra::OptRef<app::CmdFifo> fifo_;
    app::VideoPump& video_;
    app::UartPerformer uart_performer_{};
    app::IEventSink& ui_sink_;
    const infra::OptRef<app::IUiRoundPort> ui_round_;
    const infra::OptRef<app::input::MenuKeyInbox> menu_keys_;
    UiLinkFan fan_;
    const infra::OptRef<app::LwOsdEndpoint> lw_osd_;
    app::LinkRxChannel<SeatTag::Ui>& ui_rx_;

    std::optional<app::SaveStateUiTicker> save_state_ticker_{};
    std::optional<app::UiWriteTicker> ui_write_ticker_{};
    Roster coworkers_{};
    xthread::WakeFlag& wake_;
    xthread::WakeFlag stop_;
    xthread::PauseLatch pause_;
    xthread::PauseLatch::Observer pause_seen_{};
    std::uint32_t paused_link_drops_ = 0;

    const infra::OptRef<app::UartModeController::Handoffs> uart_handoffs_;
    xthread::SeatPark park_;
};

static_assert(xthread::SeatBody<UiMain>);

}
