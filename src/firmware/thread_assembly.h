// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <pthread.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "app/event.h"
#include "rt_evidence.h"
#include "rt_setup.h"
#include "seat_set.h"
#include "hal/thread_map.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/telemetry.h"
#include "infra/wake_flag.h"
#include "diag_main.h"
#include "diag_sampler.h"
#include "ui_main.h"
#include "infra/seat.h"
#include "os/kernel_tid.h"

namespace mister::xthread {
class DiagLog;
struct RtStats;
}

namespace mister::app {
class CmdFifo;
class MglPump;
class VideoPump;
}

namespace mister::fw {

struct SeatBirth {
    RtSetup name{};
    RtSetup affinity{};
};

struct RtOutcome {
    std::optional<Error> failure{};
};

class ThreadAssembly {
    TASTY_SEAT_EXEMPT(boot);

public:
    struct DiagWires {
        xthread::DiagLog& log;
        xthread::LogLane& rt_lane;
        const xthread::Telemetry<bool, SeatTag::RT>& transitioning;
    };

    ThreadAssembly(
        const hal::ThreadMap& threads,
        xthread::RtStats& stats,
        app::EventQueue<SeatTag::RT>& events,
        RtEvidence& boot,
        xthread::WakeFlag& main_wake,
        const DiagWires& diag,
        const UiMain::Wires& ui_wires,
        const UiMain::Wiring& ui,
        const DiagSampler::Sources& diag_sources,
        const DiagSampler::Services& diag_services,
        SeatContract contract = SeatContract::Declared
    ) noexcept;

    ThreadAssembly(
        const hal::ThreadMap&,
        xthread::RtStats&,
        app::EventQueue<SeatTag::RT>&,
        RtEvidence&&,
        xthread::WakeFlag&,
        const DiagWires&,
        const UiMain::Wires&,
        const UiMain::Wiring&,
        const DiagSampler::Sources&,
        const DiagSampler::Services&,
        SeatContract = SeatContract::Declared
    ) = delete;
    ~ThreadAssembly();

    ThreadAssembly(const ThreadAssembly&) = delete;
    ThreadAssembly& operator=(const ThreadAssembly&) = delete;

    DiagSampler& diag() noexcept { return diag_sampler_; }

    const DiagSampler& diag() const noexcept { return diag_sampler_; }

    xthread::DiagLog& diag_log() noexcept { return diag_log_; }

    [[nodiscard]] Ex<void> born() const noexcept { return born_; }

    infra::OptRef<UiMain> ui() noexcept {
        return ui_main_ ? infra::OptRef<UiMain>{*ui_main_} : infra::OptRef<UiMain>{};
    }

    using SeatMains = MainSlots;

    [[nodiscard]] Ex<void> spawn(RtMode mode, const SeatMains& mains);

    [[nodiscard]] bool any_live() const noexcept;

    [[nodiscard]] Ex<void> spawn_rt(RtMode mode, app::RtMain& rt);

    xthread::WakeFlag& supervisor_wake() noexcept { return main_wake_; }

    [[nodiscard]] bool rt_exited() const noexcept {
        return rt_exited_.load(std::memory_order_acquire);
    }

    [[nodiscard]] Ex<void> join_rt_within(std::chrono::nanoseconds deadline);
    [[nodiscard]] Ex<void> join_io_within(std::chrono::nanoseconds deadline);

    [[nodiscard]] Ex<void> stop_and_join_rt(std::chrono::nanoseconds deadline);

    [[nodiscard]] Ex<void> rt_result() const noexcept {
        const auto outcome = rt_outcome_.sample();
        if (outcome && outcome.value.failure)
            return std::unexpected(*outcome.value.failure);
        return {};
    }

    struct StopBudget {
        std::chrono::nanoseconds rt;
        std::chrono::nanoseconds io;
    };

    void stop() noexcept;
    [[nodiscard]] Ex<void> join();
    [[nodiscard]] Ex<void> join(StopBudget budget);

    void stop_join_or_exit() noexcept;
    void stop_join_or_exit(StopBudget budget) noexcept;

    void mark_quiescing() noexcept;

    const RtEvidence& evidence() const noexcept { return ev_; }

    [[nodiscard]] std::optional<os::KernelTid> tid(SeatTag s) const noexcept {
        const os::KernelTid published = seats_[seat_index(s)].tid.load(std::memory_order_acquire);
        if (published == kNoTidPublished)
            return std::nullopt;
        return published;
    }

    [[nodiscard]] bool io_wedged() const noexcept { return io_poisoned_; }

    [[nodiscard]] bool has_io_seat() const noexcept { return mains_.get<svc::IoMain>() != nullptr; }

private:
    static DiagSampler::Sources with_ui_cells_(
        DiagSampler::Sources s,
        const UiMain::Wiring& ui
    ) noexcept;

    [[nodiscard]] SeatBirth adopt_seat(SeatTag s) noexcept;

    void collect_births_() noexcept;

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

    SeatContract contract_ = SeatContract::Declared;

    [[nodiscard]] Ex<void> check_presence_() const noexcept;

    RtEvidence& ev_;
    const hal::ThreadMap& threads_;

    std::array<char, hal::kCommNameMax + 1> comm_prefix_ = process_comm_();

    struct LiveSeat {
        pthread_t handle{};
        std::atomic<os::KernelTid> tid{};
        bool live = false;
        xthread::Telemetry<SeatBirth> birth{};
    };

    static constexpr os::KernelTid kNoTidPublished{};
    std::array<LiveSeat, hal::kThreadSeats> seats_{};
    bool io_poisoned_ = false;

    xthread::Telemetry<RtOutcome, SeatTag::RT> rt_outcome_{};
    xthread::WakeFlag& main_wake_;

    xthread::DiagLog& diag_log_;

    std::atomic<bool> rt_exited_{false};
    std::atomic<bool> io_exited_{false};
    xthread::WakeFlag quiescing_;
    Ex<void> born_{};

    void create_mains_(
        app::EventQueue<SeatTag::RT>& events,
        const UiMain::Wires& ui_wires,
        const UiMain::Wiring& ui
    ) noexcept;

    void publish_evidence_() noexcept;

    xthread::Telemetry<RtEvidence, SeatTag::Main> ev_cell_{};

    DiagSampler diag_sampler_;
    xthread::WakeFlag diag_wake_{};
    std::optional<DiagMain> diag_main_;
    std::optional<UiMain> ui_main_;
};

}
