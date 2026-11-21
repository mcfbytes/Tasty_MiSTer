// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include <sys/epoll.h>

#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/rt_latency.h"
#include "infra/xthread/rt_stats.h"
#include "infra/xthread/telemetry.h"
#include "infra/unique_fd.h"
#include "infra/xthread/wake_flag.h"
#include "reactor/core_state.h"
#include "reactor/deadline_miss_sink.h"
#include "reactor/round_timer.h"
#include "reactor/service_slot.h"
#include "reactor/tick_deadline.h"
#include "reactor/tick_no.h"
#include "reactor/types.h"
#include "reactor/link_decoder_decl.h"
#include "infra/seat.h"

namespace mister::reactor {

class Executive {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::size_t kMaxBorrowedFds = 16;

    struct Wiring {
        infra::OptRef<RoundTimer> round_timer{};

        infra::OptRef<IDeadlineMissSink> miss_log{};
    };

    [[nodiscard]] static Ex<Executive> create(xthread::RtStats& stats, const Wiring& wiring);
    [[nodiscard]] static Ex<Executive> create(xthread::RtStats& stats);

    [[nodiscard]] Ex<void> bind(std::span<const LinkDecoderDecl> services, CoreState& state);

    [[nodiscard]] Ex<void> add_fd(int fd);

    [[nodiscard]] Ex<void> remove_fd(int fd);

    void enter() noexcept;

    void block() noexcept;

    bool dispatch_ready() noexcept;

    [[nodiscard]] bool take_stop() noexcept;

    void service_round() noexcept;

    [[nodiscard]] bool round_unclaimed() const noexcept { return !round_claimed_; }

    void end_round(bool wrote) noexcept;

    void settle() noexcept;

    [[nodiscard]] Ex<void> fault() const noexcept { return fault_; }

    void stop() noexcept;

    void kick();

    [[nodiscard]] xthread::WakeFlag::Kicker kicker() noexcept { return wake_.kicker(); }

    std::size_t borrowed_high_water() const noexcept { return borrowed_count_; }

    [[nodiscard]] const infra::RtLatency& latency() const noexcept { return latency_; }

    std::size_t service_count() const noexcept { return service_count_; }

    void service_on_caller() noexcept;

    [[nodiscard]] bool run_entered() const noexcept { return entered_.generation().held(); }

    [[nodiscard]] std::uint32_t caller_pass_refusals() const noexcept {
        return caller_pass_refusals_;
    }

    int epoll_fd() const noexcept { return epfd_.get(); }

    static constexpr std::uint8_t kClaimStreakMax = 4;

    std::uint32_t claim_yields() const noexcept { return claim_yields_; }

    Executive(Executive&&) noexcept;
    Executive(const Executive&) = delete;
    Executive& operator=(const Executive&) = delete;
    Executive& operator=(Executive&&) = delete;

private:
    friend struct ExecutiveTestAccess;

    Executive(xthread::RtStats& stats, const Wiring& wiring)
        : stats_(stats),
          miss_log_(wiring.miss_log),
          round_timer_(wiring.round_timer) {}

    [[nodiscard]] ServiceSlot live_end() const noexcept {
        const std::optional<ServiceSlot> end = ServiceSlot::of(service_count_);
        return end.value_or(ServiceSlot{});
    }

    void service_round(bool swept);
    void run_service(ServiceSlot row, RoundSegment seg);

    xthread::RtStats& stats_;
    infra::RtLatency latency_{};
    UniqueFd epfd_;
    xthread::WakeFlag wake_;

    struct RowState {
        TASTY_SEAT_RESIDENT(RT);
        TickNo next_due{};
        std::uint8_t claim_streak = 0;
    };

    infra::IndexedArray<LinkDecoderDecl, kMaxServices, ServiceSlot> services_{};
    infra::IndexedArray<RowState, kMaxServices, ServiceSlot> rows_{};
    std::uint32_t claim_yields_ = 0;
    std::size_t service_count_ = 0;
    infra::OptRef<CoreState> core_state_;
    int borrowed_[kMaxBorrowedFds] = {};

    std::size_t borrowed_count_ = 0;
    TickNo tick_count_{};
    TickDeadline tick_ = TickDeadline::held();
    bool tick_held_ = false;
    infra::OptRef<IDeadlineMissSink> miss_log_;
    infra::OptRef<RoundTimer> round_timer_;
    bool bound_in_round_ = false;

    bool round_claimed_ = false;
    bool swept_ = false;
    MonoInstant t_wake_{};
    static constexpr int kMaxEvents = 16;
    epoll_event evs_[kMaxEvents] = {};
    int n_ready_ = 0;
    Ex<void> fault_{};

    xthread::Telemetry<bool> entered_{};
    std::uint32_t caller_pass_refusals_ = 0;

    xthread::WakeFlag::Cursor stop_seen_;
};

}
