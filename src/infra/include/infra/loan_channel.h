// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "infra/error.h"
#include "infra/seat.h"
#include "infra/slot_pool.h"
#include "infra/spsc_ring.h"
#include "infra/wake_flag.h"

namespace mister::xthread {

template <SeatTag S>
concept SeatMayBlock = (S != SeatTag::RT);

template <class Slot, std::size_t N, SeatTag OwnerSeat, SeatTag WorkerSeat>
class LoanChannel {
    TASTY_SEAT_MEDIATOR(Any, Any);
    static_assert(OwnerSeat != SeatTag::Unbound && WorkerSeat != SeatTag::Unbound,
                  "both seats are named: an Unbound side would silently opt out of the "
                  "requires-clause that keeps T-RT from sleeping on this object");
    static_assert(OwnerSeat != WorkerSeat, "a crossing needs two seats");
    static_assert(N > 0 && N <= 128, "a handoff indexes with a uint8_t and 0xFF is kNoIndex");
    static_assert((N & (N - 1)) == 0, "SpscRing requires a power-of-two capacity");
    static_assert(std::is_trivially_copyable_v<Slot>,
                  "a slot is recycled IN PLACE and never destroyed between uses: an owning "
                  "member would be released once, at teardown, on whichever seat tears the "
                  "channel down");

    using Pool = SlotPool<Slot, N>;
    using Lease = typename Pool::Lease;

public:
    struct Handoff {
        std::uint8_t index = 0;
        std::uint8_t done = 0;
    };
    static_assert(std::is_trivially_copyable_v<Handoff> && sizeof(Handoff) == 2);

    class Loan {
        TASTY_SEAT_EXEMPT(component);

    public:
        Loan() noexcept = default;
        Loan(Loan&& o) noexcept
            : ch_(std::exchange(o.ch_, nullptr)), lease_(std::move(o.lease_)), done_(o.done_) {}
        Loan& operator=(Loan&& o) noexcept {
            if (this != &o) {
                drop_();
                ch_ = std::exchange(o.ch_, nullptr);
                lease_ = std::move(o.lease_);
                done_ = o.done_;
            }
            return *this;
        }
        Loan(const Loan&) = delete;
        Loan& operator=(const Loan&) = delete;
        ~Loan() { drop_(); }

        [[nodiscard]] explicit operator bool() const noexcept { return lease_ != nullptr; }
        [[nodiscard]] Slot* get() const noexcept { return lease_.get(); }
        [[nodiscard]] Slot& operator*() const noexcept { return *checked_(); }
        [[nodiscard]] Slot* operator->() const noexcept { return checked_(); }

        [[nodiscard]] bool completed() const noexcept { return done_ != 0; }

    private:
        friend class LoanChannel;
        Loan(LoanChannel* ch, Lease l, std::uint8_t done) noexcept
            : ch_(ch), lease_(std::move(l)), done_(done) {}
        void drop_() noexcept {
            if (lease_ == nullptr) return;
            seat_assert<OwnerSeat>(ERR_SITE(), "a Loan died off the owner seat");
            bump_(ch_->loans_, -1);
            lease_.reset();
        }
        [[nodiscard]] Slot* checked_() const noexcept {
            if constexpr (kSeatChecksEnabled)
                if (lease_ == nullptr) fatal(Error{Errc::slot_range, ERR_SITE(), 0}, "empty Loan");
            return lease_.get();
        }
        LoanChannel* ch_ = nullptr;
        Lease lease_{nullptr, typename Pool::Return{}};
        std::uint8_t done_ = 0;
    };

    class Job {
        TASTY_SEAT_EXEMPT(component);

    public:
        Job() noexcept = default;
        Job(Job&& o) noexcept : ch_(std::exchange(o.ch_, nullptr)), h_(o.h_) {}
        Job& operator=(Job&& o) noexcept {
            if (this != &o) {
                finish_();
                ch_ = std::exchange(o.ch_, nullptr);
                h_ = o.h_;
            }
            return *this;
        }
        Job(const Job&) = delete;
        Job& operator=(const Job&) = delete;
        ~Job() { finish_(); }

        [[nodiscard]] explicit operator bool() const noexcept { return ch_ != nullptr; }
        [[nodiscard]] Slot* get() const noexcept {
            return ch_ == nullptr ? nullptr : &ch_->pool_.at(h_.index);
        }
        [[nodiscard]] Slot& operator*() const noexcept { return *checked_(); }
        [[nodiscard]] Slot* operator->() const noexcept { return checked_(); }
        [[nodiscard]] std::uint8_t index() const noexcept { return h_.index; }

        void complete() noexcept { h_.done = 1; }

    private:
        friend class LoanChannel;
        Job(LoanChannel* ch, Handoff h) noexcept : ch_(ch), h_(h) {}
        [[nodiscard]] Slot* checked_() const noexcept {
            if constexpr (kSeatChecksEnabled)
                if (ch_ == nullptr) fatal(Error{Errc::slot_range, ERR_SITE(), 0}, "empty Job");
            return &ch_->pool_.at(h_.index);
        }
        void finish_() noexcept {
            if (ch_ == nullptr) return;
            seat_assert<WorkerSeat>(ERR_SITE(), "a Job died off the worker seat");
            bump_(ch_->jobs_, -1);
            if (!ch_->back_.push(h_)) ch_->breaches_.fetch_add(1, std::memory_order_relaxed);
            ch_->kick_owner_();
            ch_ = nullptr;
        }
        LoanChannel* ch_ = nullptr;
        Handoff h_{};
    };

    static_assert(!std::is_trivially_copyable_v<Loan>);
    static_assert(!std::is_trivially_copyable_v<Job>);

    explicit LoanChannel(WakeFlag& worker_wake) noexcept
        requires(!SeatMayBlock<OwnerSeat> && SeatMayBlock<WorkerSeat>)
        : worker_wake_(&worker_wake) {}
    explicit LoanChannel(WakeFlag& owner_wake) noexcept
        requires(SeatMayBlock<OwnerSeat> && !SeatMayBlock<WorkerSeat>)
        : owner_wake_(&owner_wake) {}
    explicit LoanChannel(Polled) noexcept
        requires(SeatMayBlock<OwnerSeat> != SeatMayBlock<WorkerSeat>)
    {}
    LoanChannel(Polled, Polled) noexcept
        requires(SeatMayBlock<OwnerSeat> && SeatMayBlock<WorkerSeat>)
    {}
    LoanChannel(WakeFlag& worker_wake, WakeFlag& owner_wake) noexcept
        requires(SeatMayBlock<OwnerSeat> && SeatMayBlock<WorkerSeat>)
        : worker_wake_(&worker_wake), owner_wake_(&owner_wake) {}
    LoanChannel(WakeFlag& worker_wake, Polled) noexcept
        requires(SeatMayBlock<OwnerSeat> && SeatMayBlock<WorkerSeat>)
        : worker_wake_(&worker_wake) {}

    LoanChannel(const LoanChannel&) = delete;
    LoanChannel& operator=(const LoanChannel&) = delete;
    LoanChannel(LoanChannel&&) = delete;
    LoanChannel& operator=(LoanChannel&&) = delete;

    [[nodiscard]] Loan acquire() noexcept {
        seat_assert<OwnerSeat>(ERR_SITE(), "LoanChannel::acquire off the owner seat");
        auto l = pool_.acquire();
        if (l == nullptr) return Loan{};
        bump_(loans_, 1);
        return Loan{this, std::move(l), 0};
    }

    void send(Loan&& l) noexcept {
        seat_assert<OwnerSeat>(ERR_SITE(), "LoanChannel::send off the owner seat");
        Slot* const s = l.lease_.release();
        if (s == nullptr) return;
        bump_(loans_, -1);
        if (!out_.push(Handoff{pool_.index_of(s), 0})) {
            breaches_.fetch_add(1, std::memory_order_relaxed);
            (void)Lease{s, typename Pool::Return{&pool_}};
            return;
        }
        kick_worker_();
    }

    [[nodiscard]] Loan reap() noexcept {
        seat_assert<OwnerSeat>(ERR_SITE(), "LoanChannel::reap off the owner seat");
        const auto h = back_.pop();
        if (!h) return Loan{};
        bump_(loans_, 1);
        return Loan{this, Lease{&pool_.at(h->index), typename Pool::Return{&pool_}}, h->done};
    }

    static constexpr std::uint8_t kNoIndex = Pool::kNoIndex;
    [[nodiscard]] std::uint8_t index_of(const Loan& l) const noexcept {
        return l.get() == nullptr ? kNoIndex : pool_.index_of(l.get());
    }

    [[nodiscard]] Job take() noexcept {
        seat_assert<WorkerSeat>(ERR_SITE(), "LoanChannel::take off the worker seat");
        const auto h = out_.pop();
        if (!h) return Job{};
        bump_(jobs_, 1);
        return Job{this, *h};
    }

    [[nodiscard]] bool arm_worker() noexcept
        requires SeatMayBlock<WorkerSeat>
    {
        if (worker_wake_ == nullptr) return false;
        worker_wake_->arm();
        return out_.size() == 0;
    }
    void disarm_worker() noexcept
        requires SeatMayBlock<WorkerSeat>
    {
        if (worker_wake_ != nullptr) worker_wake_->disarm();
    }
    [[nodiscard]] bool arm_owner() noexcept
        requires SeatMayBlock<OwnerSeat>
    {
        if (owner_wake_ == nullptr) return false;
        owner_wake_->arm();
        return back_.size() == 0;
    }
    void disarm_owner() noexcept
        requires SeatMayBlock<OwnerSeat>
    {
        if (owner_wake_ != nullptr) owner_wake_->disarm();
    }

    [[nodiscard]] std::size_t reclaim_all_parked() noexcept {
        seat_assert<OwnerSeat>(ERR_SITE(), "LoanChannel::reclaim_all_parked off the owner seat");
        for (auto& r : reserved_)
            r.reset();
        for (std::size_t i = 0; i < N; ++i) {
            const auto h = back_.pop();
            if (!h) break;
            (void)Lease{&pool_.at(h->index), typename Pool::Return{&pool_}};
        }
        for (std::size_t i = 0; i < N; ++i) {
            const auto h = out_.pop();
            if (!h) break;
            (void)Lease{&pool_.at(h->index), typename Pool::Return{&pool_}};
        }
        jobs_.store(0, std::memory_order_relaxed);
        live_ = N;
        return pool_.available();
    }

    [[nodiscard]] bool set_live(std::uint8_t n) noexcept {
        seat_assert<OwnerSeat>(ERR_SITE(), "LoanChannel::set_live off the owner seat");
        if (n == 0 || n > N) return false;

        if (pool_.available() != live_) return false;
        for (auto& r : reserved_)
            r.reset();
        for (std::size_t i = 0; i < N; ++i)
            reserved_[i] = pool_.acquire();
        for (std::size_t i = 0; i < N; ++i)
            if (pool_.index_of(reserved_[i].get()) < n) reserved_[i].reset();
        live_ = n;
        return true;
    }

    struct Census {
        std::uint8_t idle = 0, held_by_owner = 0, outbound = 0, held_by_worker = 0, inbound = 0;
        std::uint8_t live = 0;
        std::uint32_t breaches = 0;
        [[nodiscard]] constexpr std::size_t total() const noexcept {
            return std::size_t(idle) + held_by_owner + outbound + held_by_worker + inbound;
        }
        [[nodiscard]] constexpr bool intact() const noexcept {
            return breaches == 0 && total() == live;
        }
    };

    [[nodiscard]] Census census() const noexcept {
        return Census{
            static_cast<std::uint8_t>(pool_.available()), loans_.load(std::memory_order_relaxed),
            static_cast<std::uint8_t>(out_.size()),       jobs_.load(std::memory_order_relaxed),
            static_cast<std::uint8_t>(back_.size()),      live_,
            breaches_.load(std::memory_order_relaxed)};
    }

    [[nodiscard]] std::size_t outbound() const noexcept { return out_.size(); }
    [[nodiscard]] std::uint8_t live() const noexcept { return live_; }
    static constexpr std::size_t capacity() noexcept { return N; }

private:
    friend class Loan;
    friend class Job;
    static void bump_(std::atomic<std::uint8_t>& a, int d) noexcept {
        a.store(static_cast<std::uint8_t>(a.load(std::memory_order_relaxed) + d),
                std::memory_order_relaxed);
    }
    void kick_worker_() noexcept {
        if constexpr (SeatMayBlock<WorkerSeat>) {
            if (worker_wake_ != nullptr) worker_wake_->kick_if_armed();
        }
    }
    void kick_owner_() noexcept {
        if constexpr (SeatMayBlock<OwnerSeat>) {
            if (owner_wake_ != nullptr) owner_wake_->kick_if_armed();
        }
    }

    SpscRing<Handoff, N> out_{};
    SpscRing<Handoff, N> back_{};
    WakeFlag* worker_wake_ = nullptr;
    WakeFlag* owner_wake_ = nullptr;
    std::atomic<std::uint8_t> loans_{0};
    std::atomic<std::uint8_t> jobs_{0};
    std::atomic<std::uint32_t> breaches_{0};
    std::uint8_t live_ = N;
    Pool pool_{};
    Lease reserved_[N]{};
};

}  // namespace mister::xthread
