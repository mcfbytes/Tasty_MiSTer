// SPDX-License-Identifier: GPL-3.0-or-later
#include "reactor/executive.h"

#include "reactor/core_state.h"
#include "reactor/link_decoder.h"

#include <sys/epoll.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <ctime>
#include <limits>
#include <utility>

namespace mister::reactor {

namespace {

constexpr std::int64_t kNsPerSec = 1'000'000'000;

std::int64_t now_ns() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * kNsPerSec + static_cast<std::int64_t>(ts.tv_nsec);
}

enum class FdKind : std::uint32_t { Kick = 2, Borrowed = 3 };

constexpr std::uint64_t ep_tag(FdKind k, std::uint32_t idx) noexcept {
    return (static_cast<std::uint64_t>(k) << std::numeric_limits<std::uint32_t>::digits) | idx;
}

[[nodiscard]] Ex<void> ep_add(int epfd, int fd, std::uint64_t tag) noexcept {
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.u64 = tag;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    return {};
}

[[nodiscard]] Ex<void> ep_del(int epfd, int fd) noexcept {
    epoll_event ev{};
    if (::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev) < 0) {
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    return {};
}

constexpr int kFreeSlot = -1;

}

Ex<Executive> Executive::create(xthread::RtStats& stats) {
    return create(stats, Wiring{});
}

Ex<Executive> Executive::create(xthread::RtStats& stats, const Wiring& wiring) {
    Executive ex{stats, wiring};

    ex.epfd_.reset(::epoll_create1(EPOLL_CLOEXEC));
    if (!ex.epfd_.valid()) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    if (auto w = ex.wake_.open_fd(); !w) {
        return std::unexpected(w.error());
    }
    if (auto added = ep_add(ex.epfd_.get(), ex.wake_.fd(), ep_tag(FdKind::Kick, 0)); !added) {
        return std::unexpected(added.error());
    }
    return ex;
}

Executive::Executive(Executive&& o) noexcept
    : stats_(o.stats_),
      latency_(o.latency_),
      epfd_(std::move(o.epfd_)),
      wake_(std::move(o.wake_)),
      service_count_(o.service_count_),
      core_state_(o.core_state_),
      borrowed_count_(o.borrowed_count_),
      tick_count_(o.tick_count_),
      tick_(o.tick_),
      tick_held_(o.tick_held_),
      log_lane_(o.log_lane_),
      round_timer_(o.round_timer_),
      bound_in_round_(o.bound_in_round_),
      round_claimed_(o.round_claimed_),
      swept_(o.swept_),
      t_wake_(o.t_wake_),
      n_ready_(o.n_ready_),
      fault_(o.fault_),
      caller_pass_refusals_(o.caller_pass_refusals_),
      stop_seen_(o.stop_seen_) {

    if (o.entered_.generation() != 0u)
        entered_.publish(true);
    claim_yields_ = o.claim_yields_;
    services_ = o.services_;
    rows_ = o.rows_;
    for (std::size_t i = 0; i < borrowed_count_; ++i)
        borrowed_[i] = o.borrowed_[i];
    for (int i = 0; i < kMaxEvents; ++i)
        evs_[i] = o.evs_[i];
}

Ex<void> Executive::bind(std::span<const LinkDecoderDecl> services, CoreState& state) {

    std::size_t n = 0;
    for (std::size_t i = 0; i < services.size(); ++i) {
        const bool pinned_every_round = services[i].round_claim == RoundClaim::Always
            && services[i].period == kEveryRound;
        const bool negative_period = services[i].period.count() < 0;
        if (services[i].impl == nullptr || pinned_every_round || negative_period) {
            return std::unexpected(
                Error{Errc::core_load, ERR_SITE(), static_cast<std::uint32_t>(i)}
            );
        }
        if (n >= kMaxServices) {
            return std::unexpected(
                Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(services.size())}
            );
        }
        ++n;
    }
    const ServiceSlot previous = live_end();

    rows_ = infra::IndexedArray<RowState, kMaxServices, ServiceSlot>{};
    ServiceSlot row{};
    for (const LinkDecoderDecl& s : services) {
        services_[row] = s;
        stats_.deadline_miss[row].set(0);

        rows_[row].next_due = tick_count_.after(s.period);
        row = row.next();
    }
    service_count_ = n;

    for (ServiceSlot gone = row; gone < previous; gone = gone.next()) {
        stats_.deadline_miss[gone].set(0);
    }
    core_state_ = infra::OptRef<CoreState>{state};

    bound_in_round_ = true;
    if (round_timer_)
        round_timer_->note_lifecycle(RoundLifecycle::Bind);
    return {};
}

Ex<void> Executive::add_fd(int fd) {
    if (fd < 0)
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});

    std::size_t slot = kMaxBorrowedFds;
    for (std::size_t i = 0; i < borrowed_count_; ++i) {
        if (borrowed_[i] == fd) {
            slot = i;
            break;
        }
        if (borrowed_[i] == kFreeSlot && slot == kMaxBorrowedFds)
            slot = i;
    }
    if (slot == kMaxBorrowedFds) {
        if (borrowed_count_ >= kMaxBorrowedFds) {
            return std::unexpected(
                Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(borrowed_count_)}
            );
        }
        slot = borrowed_count_;
    }
    if (auto added = ep_add(
            epfd_.get(),
            fd,
            ep_tag(FdKind::Borrowed, static_cast<std::uint32_t>(slot))
        );
        !added && added.error().detail != EEXIST) {
        return std::unexpected(added.error());
    }
    borrowed_[slot] = fd;
    if (slot == borrowed_count_)
        ++borrowed_count_;
    return {};
}

Ex<void> Executive::remove_fd(int fd) {
    if (fd < 0)
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    for (std::size_t i = 0; i < borrowed_count_; ++i) {
        if (borrowed_[i] != fd)
            continue;
        borrowed_[i] = kFreeSlot;

        if (auto removed = ep_del(epfd_.get(), fd);
            !removed && removed.error().detail != ENOENT && removed.error().detail != EBADF) {
            return std::unexpected(removed.error());
        }
        return {};
    }
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), static_cast<std::uint32_t>(fd)});
}

void Executive::enter() noexcept {

    if (entered_.generation() == 0u)
        entered_.publish(true);
    fault_ = {};

    const MonoInstant now{std::chrono::nanoseconds{now_ns()}};
    tick_ = tick_held_ ? TickDeadline::held() : TickDeadline::after(now);
}

void Executive::block() noexcept {
    for (;;) {

        const auto rel = tick_.timeout(MonoInstant{std::chrono::nanoseconds{now_ns()}});
        const timespec ts = rel.value_or(timespec{});
        const int n = ::epoll_pwait2(epfd_.get(), evs_, kMaxEvents, rel ? &ts : nullptr, nullptr);
        if (n >= 0) {
            n_ready_ = n;
            return;
        }
        const auto e = static_cast<std::uint32_t>(errno);
        if (e == EINTR)
            continue;
        n_ready_ = 0;
        fault_ = std::unexpected(Error{Errc::os, ERR_SITE(), e});
        return;
    }
}

bool Executive::dispatch_ready() noexcept {
    if (!fault_)
        return false;
    stats_.heartbeat.fetch_add(1, std::memory_order_relaxed);
    t_wake_ = now_ns();
    if (round_timer_)
        round_timer_->begin(t_wake_);
    round_claimed_ = false;
    swept_ = false;

    const MonoInstant wake{std::chrono::nanoseconds{t_wake_}};
    if (const auto due = tick_.take_due(wake)) {

        const auto jit = std::max(
            std::chrono::duration_cast<std::chrono::nanoseconds>(wake - due->latest),
            std::chrono::nanoseconds::zero()
        );
        latency_.tick_jitter.record(jit);
        if (round_timer_)
            round_timer_->wake(jit.count(), due->periods.missed());
        tick_count_ += due->periods;
        swept_ = true;
    }

    for (int i = 0; i < n_ready_; ++i) {
        const auto kind = static_cast<FdKind>(
            evs_[i].data.u64 >> std::numeric_limits<std::uint32_t>::digits
        );
        const auto idx = static_cast<std::uint32_t>(evs_[i].data.u64);
        switch (kind) {
            case FdKind::Kick:
                wake_.drain();
                break;
            case FdKind::Borrowed:

                if ((evs_[i].events & (EPOLLHUP | EPOLLERR)) != 0
                    && idx < borrowed_count_
                    && borrowed_[idx] != kFreeSlot) {
                    [[maybe_unused]] Ex<void> r = ep_del(epfd_.get(), borrowed_[idx]);
                    borrowed_[idx] = kFreeSlot;
                }
                break;
        }
    }

    bound_in_round_ = false;
    return swept_;
}

bool Executive::take_stop() noexcept {
    return !fault_ || stop_seen_.take(wake_);
}

void Executive::service_round() noexcept {
    if (round_timer_)
        round_timer_->mark(RoundSegment::Steps, now_ns());
    if (!bound_in_round_)
        service_round(swept_, t_wake_);
}

void Executive::end_round(bool wrote) noexcept {
    if (round_timer_)
        (void)round_timer_->end(wrote ? now_ns() : 0);
}

void Executive::settle() noexcept {
    if (round_timer_)
        round_timer_->flush(now_ns());
}

static_assert(kTickNs == 1'000'000);

void Executive::service_on_caller(std::int64_t now_ns) noexcept {

    if (run_entered()) {
        ++caller_pass_refusals_;
        return;
    }
    if (!bound_in_round_)
        service_round(true, now_ns);
    bound_in_round_ = false;
}

void Executive::service_round(bool swept, std::int64_t wake_ns) {

    for (ServiceSlot row{}; row < live_end(); row = row.next()) {
        const LinkDecoderDecl& s = services_[row];
        RowState& state = rows_[row];
        if (s.period == kEveryRound) {
            run_service(row, wake_ns, RoundSegment::RowSwept);
        } else if (swept && tick_count_ >= state.next_due) {
            run_service(row, wake_ns, RoundSegment::RowSwept);
        }
    }
}

void Executive::run_service(ServiceSlot row, std::int64_t wake_ns, RoundSegment seg) {
    if (!core_state_)
        return;
    const LinkDecoderDecl& s = services_[row];
    RowState& state = rows_[row];
    if (!s.impl->active())
        return;

    if (s.round_claim == RoundClaim::Always)
        round_claimed_ = true;
    const bool claims = s.round_claim == RoundClaim::OnWork;
    if (claims && state.claim_streak >= kClaimStreakMax) {

        state.claim_streak = 0;
        ++claim_yields_;
        return;
    }
    const std::uint32_t claimed = claims ? s.impl->claim_count() : 0u;
    s.impl->service(*core_state_);
    if (claims) {
        const bool claim = s.impl->claim_count() != claimed;
        if (claim)
            round_claimed_ = true;
        state.claim_streak = claim ? static_cast<std::uint8_t>(state.claim_streak + 1u) : 0u;
    }
    const std::int64_t t_done = now_ns();
    if (round_timer_)
        round_timer_->mark(seg, row, t_done);
    std::int64_t d = t_done - wake_ns;
    if (d < 0)
        d = 0;
    latency_.service_latency[row].record(std::chrono::nanoseconds{d});

    if (s.period != kEveryRound) {
        const std::int64_t budget_ns = std::chrono::nanoseconds{s.period}.count();
        if (d > budget_ns) {
            if (stats_.deadline_miss[row].get() != std::numeric_limits<std::uint32_t>::max()) {
                stats_.deadline_miss[row].add();
            }

            if (log_lane_) {
                const std::int64_t over = d - budget_ns;
                constexpr std::uint32_t kSat = std::numeric_limits<std::uint32_t>::max();
                const std::uint32_t over_sat = over > std::int64_t{kSat}
                    ? kSat
                    : static_cast<std::uint32_t>(over);
                using OverNanos = std::chrono::duration<std::uint32_t, std::nano>;
                log_lane_->push(
                    LogRec::DeadlineMiss{.over = OverNanos{over_sat}, .slot = row},
                    std::chrono::nanoseconds{wake_ns}
                );
            }
        }
    }

    if (s.period != kEveryRound)
        state.next_due = tick_count_.after(s.period);
}

void Executive::stop() noexcept {
    wake_.request();
}

void Executive::kick() {
    wake_.kick();
}

}
