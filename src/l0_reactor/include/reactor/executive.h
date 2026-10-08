// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <sys/epoll.h>

#include "infra/error.h"
#include "infra/log_lane.h"
#include "infra/opt_ref.h"
#include "infra/rt_stats.h"
#include "infra/unique_fd.h"
#include "infra/wake_flag.h"
#include "reactor/core_state.h"
#include "reactor/notifier.h"
#include "reactor/notifier_slot.h"
#include "reactor/round_timer.h"
#include "reactor/tick.h"
#include "reactor/link_decoder_decl.h"
#include "infra/seat.h"

namespace mister::reactor {

inline constexpr std::uint64_t kSilenceSlackPeriods = 1;
inline constexpr std::uint64_t kSilenceRounds = 2;

constexpr bool raise_is_recent_ms(std::uint64_t now_ms, std::uint64_t last_ms,
                                  std::uint32_t period_ms) noexcept {
    return (now_ms - last_ms) <=
           static_cast<std::uint64_t>(period_ms) * (1u + kSilenceSlackPeriods);
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
        infra::OptRef<xthread::LogLane> log_lane{};
    };
    [[nodiscard]] static Ex<Executive> create(xthread::RtStats& stats, const Wiring& wiring);
    [[nodiscard]] static Ex<Executive> create(xthread::RtStats& stats);

    Ex<void> bind(std::span<const LinkDecoderDecl> services, CoreState& state);

    [[nodiscard]] Ex<NotifierSlot> add_notifier(Notifier n);

    [[nodiscard]] Ex<void> remove_notifier(NotifierSlot slot);

    Ex<void> add_fd(int fd);

    Ex<void> remove_fd(int fd);

    [[nodiscard]] Ex<void> enter() noexcept;

    void block() noexcept;

    bool dispatch_ready() noexcept;

    [[nodiscard]] bool take_stop() noexcept;

    void service_round() noexcept;

    [[nodiscard]] bool osd_budget_open() const noexcept { return !fifo_this_round_; }
    void end_round(bool wrote) noexcept;

    void settle() noexcept;

    [[nodiscard]] Ex<void> fault() const noexcept { return fault_; }
    void stop() noexcept;

    void kick();

    std::size_t borrowed_high_water() const noexcept { return borrowed_count_; }

    std::size_t service_count() const noexcept { return service_count_; }

    void service_on_caller(std::int64_t now_ns) noexcept;

    [[nodiscard]] bool run_entered() const noexcept {
        return run_entered_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint32_t caller_pass_refusals() const noexcept {
        return caller_pass_refusals_;
    }

    int epoll_fd() const noexcept { return epfd_.get(); }
    std::size_t notifier_count() const noexcept { return notifier_count_; }

    CauseSet delivered_causes() const noexcept { return delivered_; }

    CauseSet carried_causes() const noexcept;

    std::uint32_t doorbell_fallbacks() const noexcept { return fallbacks_total_; }
    std::uint32_t doorbell_retirements() const noexcept { return retirements_total_; }

    std::uint32_t row_fallbacks(std::size_t slot) const noexcept {
        return slot < service_count_ ? row_fallbacks_[slot] : 0u;
    }
    std::uint32_t row_retirements(std::size_t slot) const noexcept {
        return slot < service_count_ ? row_retirements_[slot] : 0u;
    }

    Executive(Executive&&) noexcept;
    Executive(const Executive&) = delete;
    Executive& operator=(const Executive&) = delete;
    Executive& operator=(Executive&&) = delete;

private:
    Executive(xthread::RtStats& stats, const Wiring& wiring)
        : stats_(&stats), log_lane_(wiring.log_lane ? &*wiring.log_lane : nullptr),
          round_timer_(wiring.round_timer ? &*wiring.round_timer : nullptr) {
        for (auto& t : last_raise_)
            t = kNeverRaised;
        for (auto& t : line_raise_ms_)
            t = kNeverRaised;
    }

    void dispatch_cause(Cause c, std::size_t line, std::int64_t wake_ns);
    bool notifier_carries(const LinkDecoderDecl& s) const noexcept;
    void service_round(bool swept, std::int64_t wake_ns);
    void run_service(std::size_t slot, std::int64_t wake_ns, RoundSegment seg);
    void recompute_delivered() noexcept;

    void recompute_raise(Cause c) noexcept;

    xthread::RtStats* stats_;
    UniqueFd epfd_;
    UniqueFd tick_fd_;

    xthread::WakeFlag wake_;
    std::optional<Notifier> notifiers_[kMaxLines];
    std::size_t notifier_count_ = 0;
    CauseSet delivered_;

    static constexpr std::uint64_t kNeverRaised = ~std::uint64_t{0};
    std::uint64_t line_raise_ms_[kMaxLines] = {};
    std::uint64_t line_raise_round_[kMaxLines] = {};

    std::uint64_t last_raise_[static_cast<std::size_t>(Cause::kCount)] = {};
    std::uint64_t last_raise_round_[static_cast<std::size_t>(Cause::kCount)] = {};
    LinkDecoderDecl services_[kMaxServices] = {};
    std::uint64_t next_due_[kMaxServices] = {};

    bool row_carried_[kMaxServices] = {};
    std::uint32_t row_fallbacks_[kMaxServices] = {};
    std::uint32_t row_retirements_[kMaxServices] = {};
    std::uint32_t fallbacks_total_ = 0;
    std::uint32_t retirements_total_ = 0;
    std::size_t service_count_ = 0;
    CoreState* core_state_ = nullptr;
    int borrowed_[kMaxBorrowedFds] = {};

    std::size_t borrowed_count_ = 0;
    std::uint64_t tick_count_ = 0;
    std::uint64_t round_count_ = 0;
    std::int64_t deadline_ns_ = 0;
    xthread::LogLane* log_lane_ = nullptr;
    RoundTimer* round_timer_ = nullptr;
    bool bound_in_round_ = false;

    bool fifo_this_round_ = false;
    bool swept_ = false;
    std::int64_t t_wake_ = 0;
    static constexpr int kMaxEvents = 16;
    epoll_event evs_[kMaxEvents] = {};
    int n_ready_ = 0;
    Ex<void> fault_{};
    std::atomic<bool> run_entered_{false};
    std::uint32_t caller_pass_refusals_ = 0;

    xthread::WakeFlag::Cursor stop_seen_;
};

}  // namespace mister::reactor
