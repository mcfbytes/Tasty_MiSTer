// SPDX-License-Identifier: GPL-3.0-or-later
#include "reactor/executive.h"

#include "reactor/core_state.h"
#include "reactor/link_decoder.h"

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <cerrno>
#include <ctime>
#include <utility>

namespace mister::reactor {

namespace {

constexpr std::int64_t kNsPerSec = 1'000'000'000;

std::int64_t now_ns() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * kNsPerSec + static_cast<std::int64_t>(ts.tv_nsec);
}

enum class FdKind : std::uint32_t { Tick = 1, Kick = 2, Doorbell = 3, Borrowed = 4 };

constexpr std::uint64_t ep_tag(FdKind k, std::uint32_t idx) noexcept {
    return (static_cast<std::uint64_t>(k) << 32) | idx;
}

int ep_add(int epfd, int fd, std::uint64_t tag) noexcept {
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.u64 = tag;
    return ::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == 0 ? 0 : errno;
}

int ep_del(int epfd, int fd) noexcept {
    epoll_event ev{};
    return ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev) == 0 ? 0 : errno;
}

constexpr int kFreeSlot = -1;

constexpr std::uint32_t bumped(std::uint32_t c) noexcept { return c == 0xFFFF'FFFFu ? c : c + 1u; }

}  // namespace

Ex<Executive> Executive::create(xthread::RtStats& stats) { return create(stats, Wiring{}); }

Ex<Executive> Executive::create(xthread::RtStats& stats, const Wiring& wiring) {
    Executive ex{stats, wiring};

    ex.epfd_.reset(::epoll_create1(EPOLL_CLOEXEC));
    if (!ex.epfd_.valid()) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    ex.tick_fd_.reset(::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC));
    if (!ex.tick_fd_.valid()) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    if (auto w = ex.wake_.open_fd(); !w) {
        return std::unexpected(w.error());
    }
    if (const int e = ep_add(ex.epfd_.get(), ex.tick_fd_.get(), ep_tag(FdKind::Tick, 0)); e != 0) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(e)});
    }
    if (const int e = ep_add(ex.epfd_.get(), ex.wake_.fd(), ep_tag(FdKind::Kick, 0)); e != 0) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(e)});
    }
    return ex;
}

Executive::Executive(Executive&& o) noexcept
    : stats_(o.stats_), epfd_(std::move(o.epfd_)), tick_fd_(std::move(o.tick_fd_)),
      wake_(std::move(o.wake_)), notifier_count_(o.notifier_count_), delivered_(o.delivered_),
      service_count_(o.service_count_), core_state_(o.core_state_),
      borrowed_count_(o.borrowed_count_), tick_count_(o.tick_count_), deadline_ns_(o.deadline_ns_),
      log_lane_(o.log_lane_), round_timer_(o.round_timer_), bound_in_round_(o.bound_in_round_),
      fifo_this_round_(o.fifo_this_round_), swept_(o.swept_), t_wake_(o.t_wake_),
      n_ready_(o.n_ready_), fault_(o.fault_),
      run_entered_(o.run_entered_.load(std::memory_order_relaxed)),
      caller_pass_refusals_(o.caller_pass_refusals_), stop_seen_(o.stop_seen_) {
    round_count_ = o.round_count_;
    fallbacks_total_ = o.fallbacks_total_;
    retirements_total_ = o.retirements_total_;
    claim_yields_ = o.claim_yields_;
    for (std::size_t i = 0; i < notifier_count_; ++i)
        notifiers_[i] = std::move(o.notifiers_[i]);
    for (std::size_t i = 0; i < service_count_; ++i) {
        services_[i] = o.services_[i];
        next_due_[i] = o.next_due_[i];
        row_carried_[i] = o.row_carried_[i];
        row_fallbacks_[i] = o.row_fallbacks_[i];
        row_retirements_[i] = o.row_retirements_[i];
        claim_streak_[i] = o.claim_streak_[i];
    }
    for (std::size_t i = 0; i < borrowed_count_; ++i)
        borrowed_[i] = o.borrowed_[i];
    for (int i = 0; i < kMaxEvents; ++i)
        evs_[i] = o.evs_[i];
    for (std::size_t i = 0; i < kMaxLines; ++i) {
        line_raise_ms_[i] = o.line_raise_ms_[i];
        line_raise_round_[i] = o.line_raise_round_[i];
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(Cause::kCount); ++i) {
        last_raise_[i] = o.last_raise_[i];
        last_raise_round_[i] = o.last_raise_round_[i];
    }
}

Ex<void> Executive::bind(std::span<const LinkDecoderDecl> services, CoreState& state) {

    std::size_t n = 0;
    for (std::size_t i = 0; i < services.size(); ++i) {
        if (services[i].klass != DeadlineClass::A) continue;

        const bool pinned_every_round =
            services[i].osd_budget == OsdBudget::Pinned && services[i].period_ms == 0;
        if (services[i].impl == nullptr || pinned_every_round) {
            return std::unexpected(
                Error{Errc::core_load, ERR_SITE(), static_cast<std::uint32_t>(i)});
        }
        if (n >= kMaxServices) {
            return std::unexpected(
                Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(services.size())});
        }
        ++n;
    }
    std::size_t slot = 0;
    for (const LinkDecoderDecl& s : services) {
        if (s.klass != DeadlineClass::A) continue;
        services_[slot] = s;

        next_due_[slot] = tick_count_ + s.period_ms;

        row_carried_[slot] = false;
        row_fallbacks_[slot] = 0;
        row_retirements_[slot] = 0;
        claim_streak_[slot] = 0;
        ++slot;
    }
    service_count_ = slot;
    core_state_ = &state;

    bound_in_round_ = true;
    if (round_timer_ != nullptr) round_timer_->note_lifecycle(RoundLifecycle::Bind);
    return {};
}

Ex<NotifierSlot> Executive::add_notifier(Notifier n) {
    const int fd = n.fd();
    if (fd < 0) return std::unexpected(Error{Errc::io, ERR_SITE(), 0});

    std::size_t slot = kMaxLines;
    for (std::size_t i = 0; i < notifier_count_; ++i) {
        if (!notifiers_[i].has_value()) {
            slot = i;
            break;
        }
    }
    if (slot == kMaxLines) {
        if (notifier_count_ >= kMaxLines) {
            return std::unexpected(
                Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(notifier_count_)});
        }
        slot = notifier_count_;
    }
    notifiers_[slot] = std::move(n);

    line_raise_ms_[slot] = kNeverRaised;
    line_raise_round_[slot] = 0;
    if (const int e =
            ep_add(epfd_.get(), fd, ep_tag(FdKind::Doorbell, static_cast<std::uint32_t>(slot)));
        e != 0) {
        notifiers_[slot].reset();
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(e)});
    }
    if (slot == notifier_count_) ++notifier_count_;
    recompute_delivered();
    return NotifierSlot{slot};
}

Ex<void> Executive::remove_notifier(NotifierSlot slot) {
    if (slot.v >= notifier_count_ || !notifiers_[slot.v].has_value()) {
        return std::unexpected(
            Error{Errc::not_found, ERR_SITE(), static_cast<std::uint32_t>(slot.v)});
    }

    if (const int e = ep_del(epfd_.get(), notifiers_[slot.v]->fd());
        e != 0 && e != ENOENT && e != EBADF) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(e)});
    }
    notifiers_[slot.v].reset();
    line_raise_ms_[slot.v] = kNeverRaised;
    line_raise_round_[slot.v] = 0;
    recompute_delivered();
    return {};
}

void Executive::recompute_delivered() noexcept {
    CauseSet s;
    for (std::size_t i = 0; i < notifier_count_; ++i) {
        if (notifiers_[i].has_value()) s |= notifiers_[i]->delivers();
    }
    delivered_ = s;

    for (std::size_t c = 0; c < static_cast<std::size_t>(Cause::kCount); ++c)
        recompute_raise(static_cast<Cause>(c));
}

void Executive::recompute_raise(Cause c) noexcept {
    const std::size_t ci = static_cast<std::size_t>(c);
    std::uint64_t ms = kNeverRaised;
    std::uint64_t round = 0;
    for (std::size_t i = 0; i < notifier_count_; ++i) {
        if (!notifiers_[i].has_value() || !notifiers_[i]->delivers().has(c)) continue;
        if (line_raise_ms_[i] == kNeverRaised) continue;
        if (ms == kNeverRaised || line_raise_ms_[i] > ms) {
            ms = line_raise_ms_[i];
            round = line_raise_round_[i];
        }
    }
    last_raise_[ci] = ms;
    last_raise_round_[ci] = round;
}

Ex<void> Executive::add_fd(int fd) {
    if (fd < 0) return std::unexpected(Error{Errc::io, ERR_SITE(), 0});

    std::size_t slot = kMaxBorrowedFds;
    for (std::size_t i = 0; i < borrowed_count_; ++i) {
        if (borrowed_[i] == fd) {
            slot = i;
            break;
        }
        if (borrowed_[i] == kFreeSlot && slot == kMaxBorrowedFds) slot = i;
    }
    if (slot == kMaxBorrowedFds) {
        if (borrowed_count_ >= kMaxBorrowedFds) {
            return std::unexpected(
                Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(borrowed_count_)});
        }
        slot = borrowed_count_;
    }
    if (const int e =
            ep_add(epfd_.get(), fd, ep_tag(FdKind::Borrowed, static_cast<std::uint32_t>(slot)));
        e != 0 && e != EEXIST) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(e)});
    }
    borrowed_[slot] = fd;
    if (slot == borrowed_count_) ++borrowed_count_;
    return {};
}

Ex<void> Executive::remove_fd(int fd) {
    if (fd < 0) return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    for (std::size_t i = 0; i < borrowed_count_; ++i) {
        if (borrowed_[i] != fd) continue;
        borrowed_[i] = kFreeSlot;

        if (const int e = ep_del(epfd_.get(), fd); e != 0 && e != ENOENT && e != EBADF) {
            return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(e)});
        }
        return {};
    }
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), static_cast<std::uint32_t>(fd)});
}

Ex<void> Executive::enter() noexcept {
    run_entered_.store(true, std::memory_order_release);
    fault_ = {};

    deadline_ns_ = now_ns() + kTickNs;
    itimerspec spec{};
    spec.it_interval.tv_sec = 0;
    spec.it_interval.tv_nsec = static_cast<long>(kTickNs);
    spec.it_value.tv_sec = static_cast<time_t>(deadline_ns_ / kNsPerSec);
    spec.it_value.tv_nsec = static_cast<long>(deadline_ns_ % kNsPerSec);
    if (::timerfd_settime(tick_fd_.get(), TFD_TIMER_ABSTIME, &spec, nullptr) < 0) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    return {};
}

void Executive::block() noexcept {
    for (;;) {
        const int n = ::epoll_wait(epfd_.get(), evs_, kMaxEvents, -1);
        if (n >= 0) {
            n_ready_ = n;
            return;
        }
        const auto e = static_cast<std::uint32_t>(errno);
        if (e == EINTR) continue;
        n_ready_ = 0;
        fault_ = std::unexpected(Error{Errc::os, ERR_SITE(), e});
        return;
    }
}

bool Executive::dispatch_ready() noexcept {
    if (!fault_) return false;
    stats_->heartbeat.fetch_add(1, std::memory_order_relaxed);
    ++round_count_;
    t_wake_ = now_ns();
    if (round_timer_ != nullptr) round_timer_->begin(t_wake_);
    fifo_this_round_ = false;
    swept_ = false;

    for (int i = 0; i < n_ready_; ++i) {
        const auto kind = static_cast<FdKind>(evs_[i].data.u64 >> 32);
        const auto idx = static_cast<std::uint32_t>(evs_[i].data.u64);
        switch (kind) {
            case FdKind::Tick: {
                std::uint64_t expiries = 0;
                if (::read(tick_fd_.get(), &expiries, sizeof expiries) ==
                        static_cast<ssize_t>(sizeof expiries) &&
                    expiries > 0) {

                    const std::int64_t latest =
                        deadline_ns_ + static_cast<std::int64_t>(expiries - 1) * kTickNs;
                    std::int64_t jit = t_wake_ - latest;
                    if (jit < 0) jit = 0;
                    stats_->tick_jitter.record(std::chrono::nanoseconds{jit});
                    if (round_timer_ != nullptr) round_timer_->wake(jit, expiries - 1);
                    deadline_ns_ = latest + kTickNs;
                    tick_count_ += expiries;
                    swept_ = true;
                }
                break;
            }
            case FdKind::Kick:
                wake_.drain();
                break;
            case FdKind::Doorbell: {
                if (idx < notifier_count_ && notifiers_[idx].has_value()) {
                    const CauseDecode d = notifiers_[idx]->drain_checked();

                    if (d.refused) stats_->spurious_causes.add();

                    if (d.cause == Cause::Tick)
                        swept_ = true;
                    else
                        dispatch_cause(d.cause, idx, t_wake_);
                }
                break;
            }
            case FdKind::Borrowed:

                if ((evs_[i].events & (EPOLLHUP | EPOLLERR)) != 0 && idx < borrowed_count_ &&
                    borrowed_[idx] != kFreeSlot) {
                    [[maybe_unused]] int r = ep_del(epfd_.get(), borrowed_[idx]);
                    borrowed_[idx] = kFreeSlot;
                }
                break;
        }
    }

    bound_in_round_ = false;
    return swept_;
}

bool Executive::take_stop() noexcept { return !fault_ || stop_seen_.take(wake_); }

void Executive::service_round() noexcept {
    if (round_timer_ != nullptr) round_timer_->mark(RoundSegment::Steps, 0, now_ns());
    if (!bound_in_round_) service_round(swept_, t_wake_);
}

void Executive::end_round(bool wrote) noexcept {
    if (round_timer_ != nullptr) (void)round_timer_->end(wrote ? now_ns() : 0);
}

void Executive::settle() noexcept {
    if (round_timer_ != nullptr) round_timer_->flush(now_ns());

    itimerspec disarm{};
    [[maybe_unused]] int r = ::timerfd_settime(tick_fd_.get(), 0, &disarm, nullptr);
}

void Executive::dispatch_cause(Cause c, std::size_t line, std::int64_t wake_ns) {

    if (c == Cause::None || c == Cause::Tick) return;
    if (line < kMaxLines) {
        line_raise_ms_[line] = tick_count_;
        line_raise_round_[line] = round_count_;
    }
    last_raise_[static_cast<std::size_t>(c)] = tick_count_;
    last_raise_round_[static_cast<std::size_t>(c)] = round_count_;

    for (std::size_t i = 0; i < service_count_; ++i)
        if (services_[i].cause == c) run_service(i, wake_ns, RoundSegment::RowDoorbell);
}

bool Executive::notifier_carries(const LinkDecoderDecl& s) const noexcept {
    if (s.cause == Cause::Tick || !delivered_.has(s.cause)) return false;
    const std::size_t ci = static_cast<std::size_t>(s.cause);
    if (last_raise_[ci] == kNeverRaised) return false;
    if (s.period_ms == 0) return raise_is_recent_rounds(round_count_, last_raise_round_[ci]);
    return raise_is_recent_ms(tick_count_, last_raise_[ci], s.period_ms);
}

CauseSet Executive::carried_causes() const noexcept {
    CauseSet s;
    for (std::size_t i = 0; i < service_count_; ++i)
        if (notifier_carries(services_[i])) s |= CauseSet{services_[i].cause};
    return s;
}

void Executive::service_on_caller(std::int64_t now_ns) noexcept {

    if (run_entered_.load(std::memory_order_acquire)) {
        ++caller_pass_refusals_;
        return;
    }
    if (!bound_in_round_) service_round(true, now_ns);
    bound_in_round_ = false;
}

void Executive::service_round(bool swept, std::int64_t wake_ns) {

    for (std::size_t i = 0; i < service_count_; ++i) {
        const LinkDecoderDecl& s = services_[i];
        const bool carried = notifier_carries(s);
        if (carried != row_carried_[i]) {

            std::uint32_t& per_row = carried ? row_retirements_[i] : row_fallbacks_[i];
            std::uint32_t& total = carried ? retirements_total_ : fallbacks_total_;
            per_row = bumped(per_row);
            total = bumped(total);
            row_carried_[i] = carried;
        }
        if (carried) continue;
        if (s.period_ms == 0) {
            run_service(i, wake_ns, RoundSegment::RowSwept);
        } else if (swept && tick_count_ >= next_due_[i]) {
            run_service(i, wake_ns, RoundSegment::RowSwept);
        }
    }
}

void Executive::run_service(std::size_t slot, std::int64_t wake_ns, RoundSegment seg) {
    if (core_state_ == nullptr) return;
    const LinkDecoderDecl& s = services_[slot];
    if (!s.impl->active()) return;

    if (s.osd_budget == OsdBudget::Pinned) fifo_this_round_ = true;
    const bool claims = s.osd_budget == OsdBudget::Claimed;
    if (claims && claim_streak_[slot] >= kClaimStreakMax) {

        claim_streak_[slot] = 0;
        ++claim_yields_;
        return;
    }
    const std::uint32_t claimed = claims ? s.impl->osd_claims() : 0u;
    s.impl->service(*core_state_);
    if (claims) {
        const bool claim = s.impl->osd_claims() != claimed;
        if (claim) fifo_this_round_ = true;
        claim_streak_[slot] = claim ? static_cast<std::uint8_t>(claim_streak_[slot] + 1u) : 0u;
    }
    const std::int64_t t_done = now_ns();
    if (round_timer_ != nullptr) round_timer_->mark(seg, slot, t_done);
    std::int64_t d = t_done - wake_ns;
    if (d < 0) d = 0;
    stats_->service_latency[slot].record(std::chrono::nanoseconds{d});

    if (s.period_ms != 0) {
        const std::int64_t budget_ns = static_cast<std::int64_t>(s.period_ms) * 1'000'000;
        if (d > budget_ns) {
            if (stats_->deadline_miss[slot].get() != 0xFFFFFFFFu) {
                stats_->deadline_miss[slot].add();
            }

            if (log_lane_ != nullptr) {
                const std::int64_t over = d - budget_ns;
                const std::uint32_t over_sat = over > static_cast<std::int64_t>(0xFFFFFFFFu)
                                                   ? 0xFFFFFFFFu
                                                   : static_cast<std::uint32_t>(over);
                (void)log_lane_->push(
                    LogRec::DeadlineMiss{.over_ns = over_sat,
                                         .slot = static_cast<std::uint16_t>(slot)},
                    static_cast<std::uint64_t>(wake_ns));
            }
        }
    }

    if (s.period_ms != 0) next_due_[slot] = tick_count_ + s.period_ms;
}

void Executive::stop() noexcept { wake_.request(); }

void Executive::kick() { wake_.kick(); }

}  // namespace mister::reactor
