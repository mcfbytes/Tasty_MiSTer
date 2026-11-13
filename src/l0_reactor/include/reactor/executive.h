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
#include "infra/log_lane.h"
#include "infra/opt_ref.h"
#include "infra/rt_latency.h"
#include "infra/rt_stats.h"
#include "infra/telemetry.h"
#include "infra/unique_fd.h"
#include "infra/wake_flag.h"
#include "reactor/core_state.h"
#include "reactor/notifier.h"
#include "reactor/notifier_slot.h"
#include "reactor/round_timer.h"
#include "reactor/service_slot.h"
#include "reactor/tick_deadline.h"
#include "reactor/tick_no.h"
#include "reactor/link_decoder_decl.h"
#include "infra/seat.h"

namespace mister::reactor {

inline constexpr std::uint64_t kSilenceSlackPeriods = 1;
inline constexpr std::uint64_t kSilenceRounds = 2;

constexpr bool raise_is_recent(
    std::chrono::milliseconds now,
    std::chrono::milliseconds last,
    std::chrono::milliseconds period
) noexcept {
    return (now - last) <= period * static_cast<std::int64_t>(1u + kSilenceSlackPeriods);
}

constexpr bool raise_is_recent_rounds(std::uint64_t now_round, std::uint64_t last_round) noexcept {
    return (now_round - last_round) <= kSilenceRounds;
}

class Executive {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::size_t kMaxBorrowedFds = 16;

    struct Wiring {
        infra::OptRef<RoundTimer> round_timer{};

        std::optional<xthread::LogLane::Producer> log_lane{};
    };

    [[nodiscard]] static Ex<Executive> create(xthread::RtStats& stats, const Wiring& wiring);
    [[nodiscard]] static Ex<Executive> create(xthread::RtStats& stats);

    [[nodiscard]] Ex<void> bind(std::span<const LinkDecoderDecl> services, CoreState& state);

    [[nodiscard]] Ex<NotifierSlot> add_notifier(Notifier n);

    [[nodiscard]] Ex<void> remove_notifier(NotifierSlot slot);

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

    [[nodiscard]] const xthread::RtLatency& latency() const noexcept { return latency_; }

    std::size_t service_count() const noexcept { return service_count_; }

    void service_on_caller(std::int64_t now_ns) noexcept;

    [[nodiscard]] bool run_entered() const noexcept { return entered_.generation() != 0u; }

    [[nodiscard]] std::uint32_t caller_pass_refusals() const noexcept {
        return caller_pass_refusals_;
    }

    int epoll_fd() const noexcept { return epfd_.get(); }

    std::size_t notifier_count() const noexcept { return notifier_count_; }

    CauseSet delivered_causes() const noexcept { return delivered_; }

    CauseSet carried_causes() const noexcept;

    std::uint32_t doorbell_fallbacks() const noexcept { return fallbacks_total_; }

    std::uint32_t doorbell_retirements() const noexcept { return retirements_total_; }

    std::uint32_t row_fallbacks(ServiceSlot row) const noexcept {
        const auto state = rows_.find(row);
        return state ? state->fallbacks : 0u;
    }

    std::uint32_t row_retirements(ServiceSlot row) const noexcept {
        const auto state = rows_.find(row);
        return state ? state->retirements : 0u;
    }

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
          log_lane_(wiring.log_lane),
          round_timer_(wiring.round_timer) {
        for (auto& t : last_raise_)
            t = RaiseStamp{};
    }

    static constexpr TickNo kNeverRaised = kNeverTick;

    [[nodiscard]] ServiceSlot live_end() const noexcept {
        const std::optional<ServiceSlot> end = ServiceSlot::of(service_count_);
        return end.value_or(ServiceSlot{});
    }

    void dispatch_cause(Cause c, NotifierSlot line, std::int64_t wake_ns);
    bool notifier_carries(const LinkDecoderDecl& s) const noexcept;
    void service_round(bool swept, std::int64_t wake_ns);
    void run_service(ServiceSlot row, std::int64_t wake_ns, RoundSegment seg);
    void recompute_delivered() noexcept;

    void recompute_raise(Cause c) noexcept;

    xthread::RtStats& stats_;
    xthread::RtLatency latency_{};
    UniqueFd epfd_;
    xthread::WakeFlag wake_;

    struct RaiseStamp {
        TASTY_SEAT_RESIDENT(RT);
        TickNo ms = kNeverRaised;
        std::uint64_t round = 0;
    };

    class Line {
        TASTY_SEAT_RESIDENT(RT);

    public:
        [[nodiscard]] bool bound() const noexcept { return notifier_.has_value(); }

        void bind(Notifier n) noexcept {
            notifier_ = std::move(n);
            clear_evidence_();
        }

        void unbind() noexcept {
            notifier_.reset();
            clear_evidence_();
        }

        [[nodiscard]] Notifier& notifier() noexcept { return *notifier_; }

        [[nodiscard]] const Notifier& notifier() const noexcept { return *notifier_; }

        [[nodiscard]] bool delivers(Cause c) const noexcept {
            return notifier_.has_value() && notifier_->delivers().has(c);
        }

        [[nodiscard]] bool has_evidence() const noexcept { return stamp_.ms != kNeverRaised; }

        void raised(const RaiseStamp& at) noexcept { stamp_ = at; }

        [[nodiscard]] const RaiseStamp& stamp() const noexcept { return stamp_; }

    private:
        void clear_evidence_() noexcept { stamp_ = RaiseStamp{}; }

        std::optional<Notifier> notifier_{};
        RaiseStamp stamp_{};
    };

    struct RowState {
        TASTY_SEAT_RESIDENT(RT);
        TickNo next_due{};
        bool carried = false;
        std::uint32_t fallbacks = 0;
        std::uint32_t retirements = 0;
        std::uint8_t claim_streak = 0;
    };

    std::size_t notifier_count_ = 0;
    CauseSet delivered_;

    RaiseStamp last_raise_[static_cast<std::size_t>(Cause::kCount)] = {};

    infra::IndexedArray<LinkDecoderDecl, kMaxServices, ServiceSlot> services_{};
    infra::IndexedArray<RowState, kMaxServices, ServiceSlot> rows_{};
    std::uint32_t claim_yields_ = 0;
    std::uint32_t fallbacks_total_ = 0;
    std::uint32_t retirements_total_ = 0;
    std::size_t service_count_ = 0;
    infra::OptRef<CoreState> core_state_;
    int borrowed_[kMaxBorrowedFds] = {};

    std::size_t borrowed_count_ = 0;
    TickNo tick_count_{};
    std::uint64_t round_count_ = 0;
    TickDeadline tick_ = TickDeadline::held();
    bool tick_held_ = false;
    std::optional<xthread::LogLane::Producer> log_lane_;
    infra::OptRef<RoundTimer> round_timer_;
    bool bound_in_round_ = false;

    bool round_claimed_ = false;
    bool swept_ = false;
    std::int64_t t_wake_ = 0;
    static constexpr int kMaxEvents = 16;
    epoll_event evs_[kMaxEvents] = {};
    int n_ready_ = 0;
    Ex<void> fault_{};

    xthread::Telemetry<bool> entered_{};
    std::uint32_t caller_pass_refusals_ = 0;

    xthread::WakeFlag::Cursor stop_seen_;

    infra::IndexedArray<Line, kMaxLines, NotifierSlot> lines_{};
};

}
