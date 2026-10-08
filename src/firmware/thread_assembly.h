// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <pthread.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "rt_evidence.h"
#include "seat_set.h"
#include "hal/thread_map.h"
#include "infra/error.h"
#include "infra/wake_flag.h"
#include "diag_main.h"
#include "diag_sampler.h"
#include "ui_main.h"
#include "infra/seat.h"

namespace mister::xthread {
struct RtStats;
}
namespace mister::app {
class CmdFifo;
class EventQueue;
class InputBuild;
class MglPump;
class VideoPump;
class BtPump;
}  // namespace mister::app

namespace mister::fw {

enum class RtMode : std::uint8_t;

class ThreadAssembly {
    TASTY_SEAT_EXEMPT(boot);

public:
    struct DiagWires {
        xthread::DiagLog& log;
        xthread::LogLane& rt_lane;
        const std::atomic<bool>& transitioning;
    };

    ThreadAssembly(const hal::ThreadMap& threads, xthread::RtStats& stats, app::EventQueue& events,
                   RtEvidence& boot, xthread::WakeFlag& main_wake, const DiagWires& diag,
                   const UiMain::Wires& ui_wires, const UiMain::Wiring& ui,
                   const DiagSampler::Sources& diag_sources) noexcept;

    ThreadAssembly(const hal::ThreadMap&, xthread::RtStats&, app::EventQueue&, RtEvidence&&,
                   xthread::WakeFlag&, const DiagWires&, const UiMain::Wires&,
                   const UiMain::Wiring&, const DiagSampler::Sources&) = delete;

    ThreadAssembly(xthread::RtStats& stats, app::EventQueue& events, const UiMain::Wiring& ui,
                   const DiagSampler::Sources& diag) noexcept;
    ~ThreadAssembly();

    ThreadAssembly(const ThreadAssembly&) = delete;
    ThreadAssembly& operator=(const ThreadAssembly&) = delete;

    DiagSampler& diag() noexcept { return diag_sampler_; }
    const DiagSampler& diag() const noexcept { return diag_sampler_; }

    UiMain* ui() noexcept { return ui_main_ ? &*ui_main_ : nullptr; }

    struct SeatMains {
        MainSlots mains{};
        app::InputBuild* input_build = nullptr;
    };

    Ex<void> spawn(RtMode mode, const SeatMains& mains);

    [[nodiscard]] bool any_live() const noexcept;

    [[nodiscard]] Ex<void> spawn_rt(RtMode mode, app::RtMain& rt);

    xthread::WakeFlag& supervisor_wake() noexcept { return main_wake_; }

    [[nodiscard]] bool rt_exited() const noexcept {
        return rt_exited_.load(std::memory_order_acquire);
    }

    [[nodiscard]] Ex<void> join_rt_within(std::int64_t deadline_ns);
    [[nodiscard]] Ex<void> join_io_within(std::int64_t deadline_ns);

    [[nodiscard]] Ex<void> stop_and_join_rt(std::int64_t deadline_ns);

    [[nodiscard]] Ex<void> rt_result() const noexcept { return rt_result_; }

    struct StopBudget {
        std::int64_t rt_ns;
        std::int64_t io_ns;
    };

    void stop() noexcept;
    [[nodiscard]] Ex<void> join();
    [[nodiscard]] Ex<void> join(StopBudget budget);

    void stop_join_or_exit() noexcept;
    void stop_join_or_exit(StopBudget budget) noexcept;

    void mark_quiescing() noexcept;

    const RtEvidence& evidence() const noexcept { return ev_; }

    long tid(SeatTag s) const noexcept {
        return tid_[seat_index(s)].load(std::memory_order_acquire);
    }

    [[nodiscard]] bool io_wedged() const noexcept { return io_poisoned_; }

    [[nodiscard]] bool has_io_seat() const noexcept { return mains_.get<svc::IoMain>() != nullptr; }

private:
    static DiagSampler::Sources with_ui_cells_(DiagSampler::Sources s,
                                               const UiMain::Wiring& ui) noexcept;

    void adopt_seat(SeatTag s) noexcept;

    static std::array<char, hal::kCommNameMax + 1> process_comm_() noexcept;

    template <class Row>
    static void* trampoline(void* self);

    struct SeatTable;

    [[noreturn]] void exit_seats_live_() const noexcept;

    void stop_bound_(SeatTag s) noexcept;
    template <class... Rows>
    void stop_seat_(SeatTag s, SeatList<Rows...>) noexcept;
    template <class Row>
    void stop_one_() noexcept;

    MainSlots mains_{};

    RtEvidence own_ev_{};
    RtEvidence& ev_;
    const hal::ThreadMap& threads_;

    std::array<char, hal::kCommNameMax + 1> comm_prefix_ = process_comm_();
    std::array<pthread_t, hal::kThreadSeats> thread_{};
    std::array<bool, hal::kThreadSeats> live_{};
    bool io_poisoned_ = false;
    std::array<std::atomic<long>, hal::kThreadSeats> tid_{};

    Ex<void> rt_result_{};
    xthread::WakeFlag own_main_wake_{};
    xthread::WakeFlag& main_wake_;

    struct OwnWires {
        xthread::DiagLog log;
        xthread::LogLane rt_lane;
        std::atomic<bool> transitioning{false};
        xthread::WakeFlag ui_wake;
        app::UartModeController::Handoffs uart_handoffs{ui_wake};
    };
    std::unique_ptr<OwnWires> own_wires_;

    std::atomic<bool> rt_exited_{false};
    std::atomic<bool> io_exited_{false};
    xthread::WakeFlag quiescing_;
    Ex<void> born_{};

    void create_mains_(app::EventQueue& events, const UiMain::Wires& ui_wires,
                       const UiMain::Wiring& ui) noexcept;

    DiagSampler diag_sampler_;
    xthread::WakeFlag diag_wake_{};
    std::optional<DiagMain> diag_main_;
    std::optional<UiMain> ui_main_;
};

}  // namespace mister::fw
