// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "infra/error.h"
#include "infra/inbox.h"
#include "infra/rt_stats.h"
#include "infra/slot_pool.h"
#include "infra/loan_channel.h"
#include "infra/spsc_ring.h"
#include "infra/wake_flag.h"
#include "svc/chd_source.h"
#include "infra/seat.h"

namespace mister::svc {

inline constexpr std::uint32_t kNoPrefetchHunk = 0xFFFFFFFFu;

struct PrefetchSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);
    std::uint32_t hunk = kNoPrefetchHunk;
};

static_assert(sizeof(PrefetchSlot) == 4, "item: the slot names a hunk; the INDEX names the buffer");

inline constexpr std::size_t kPrefetchMinDepth = 8;
inline constexpr std::size_t kPrefetchMaxDepth = 64;
inline constexpr std::size_t kPrefetchBudgetBytes = 16u * 1024u * 1024u;

inline constexpr std::uint32_t kPrefetchLookBehind = 16;
inline constexpr std::uint32_t kPrefetchForwardSpan = 16;

inline constexpr std::size_t kPrefetchRingFloor = kPrefetchLookBehind + kPrefetchForwardSpan;

inline constexpr int kPrefetchTopUpMs = 40;

static_assert(
    (kPrefetchMaxDepth & (kPrefetchMaxDepth - 1)) == 0,
    "xthread::SpscRing requires a power-of-two capacity"
);
static_assert(kPrefetchMinDepth <= kPrefetchMaxDepth, "the floor cannot exceed the ceiling");
static_assert(
    kPrefetchForwardSpan >= kPrefetchMinDepth,
    "the speculation budget cannot be tighter than the SPRINT floor, or the sprint "
    "goes idle before it satisfies the three measured floors item pins it to"
);
static_assert(
    kPrefetchLookBehind + kPrefetchForwardSpan <= kPrefetchMaxDepth,
    "both budgets are carved out of the ring at its widest"
);
static_assert(
    kPrefetchLookBehind < kPrefetchRingFloor,
    "the look-behind is carved OUT of the ring at every derivable depth: it cannot "
    "consume the whole thing even at the floor, or the producer has no runway at all"
);

constexpr std::size_t derive_depth(std::size_t budget, std::size_t hunk_bytes) noexcept {
    if (hunk_bytes == 0)
        return 0;
    const std::size_t raw = budget / hunk_bytes;
    if (raw < kPrefetchRingFloor)
        return 0;
    return raw < kPrefetchMaxDepth ? raw : kPrefetchMaxDepth;
}

static_assert(
    kPrefetchRingFloor - kPrefetchLookBehind >= 1,
    "at the floor: the producer must keep at least one hunk of runway"
);
static_assert(
    kPrefetchRingFloor <= kPrefetchMaxDepth,
    "the floor cannot exceed the ring's compile-time capacity"
);
static_assert(
    kPrefetchMaxDepth - kPrefetchLookBehind >= kPrefetchRingFloor - kPrefetchLookBehind,
    "at the ceiling: the forward span is monotone non-decreasing in depth"
);

inline constexpr std::size_t kChdMediaLeases = 2;
inline constexpr std::size_t kChdAttachDepth = 4;
static_assert(kChdMediaLeases < kChdAttachDepth);
static_assert(
    (kChdAttachDepth & (kChdAttachDepth - 1)) == 0,
    "xthread::SpscRing requires a power-of-two capacity"
);

struct ChdMedia {
    TASTY_SEAT_MEDIATOR(Any, Any);
    std::unique_ptr<IChdSource> src{};

    std::uint32_t hunk_bytes = 0;
    std::uint32_t hunk_count = 0;
    std::uint32_t forward_span = 0;
};

struct ChdAttach {
    TASTY_SEAT_EXEMPT(component);
    std::uint8_t lease = 0;
};

struct PrefetchCounters {
    std::uint32_t hits = 0;
    std::uint32_t misses = 0;
    std::uint32_t decodes = 0;
    std::uint32_t seeks = 0;
    std::uint32_t drops = 0;
    std::uint32_t errors = 0;
    std::uint32_t state = 0;

    std::uint32_t decode_us = 0;
    std::uint32_t evictions = 0;
};

class ChdPrefetch {
    using Chan = xthread::
        LoanChannel<PrefetchSlot, kPrefetchMaxDepth, SeatTag::Io, SeatTag::Prefetch>;

    TASTY_SEAT_MEDIATOR(Prefetch, Io);

public:
    ChdPrefetch() = default;
    ~ChdPrefetch() = default;

    ChdPrefetch(const ChdPrefetch&) = delete;
    ChdPrefetch& operator=(const ChdPrefetch&) = delete;
    ChdPrefetch(ChdPrefetch&&) = delete;
    ChdPrefetch& operator=(ChdPrefetch&&) = delete;

    [[nodiscard]] Ex<void> open();

    [[nodiscard]] bool opened() const noexcept { return wake_.fd() >= 0; }

    [[nodiscard]] xthread::WakeFlag& wake() noexcept { return wake_; }

    void serve() noexcept;

    [[nodiscard]] bool idle() const noexcept;

    [[nodiscard]] int park_ms() const noexcept { return rest_ms_; }

    void release() noexcept;

    void park_now() noexcept;

    void request_park() noexcept;

    [[nodiscard]] bool park_acked() noexcept;

    [[nodiscard]] bool settle_park() noexcept;

    [[nodiscard]] Ex<void> attach(
        std::unique_ptr<IChdSource> src,
        std::uint32_t hunk_bytes,
        std::uint32_t hunk_count
    ) noexcept;

    bool parked() const noexcept { return parked_.load(std::memory_order_acquire); }

    template <class Flag>
    [[nodiscard]] static bool park_due(const Flag& parked, const Flag& park_req) noexcept {
        return !parked.load(std::memory_order_acquire) && park_req.load(std::memory_order_acquire);
    }

    bool attached() const noexcept { return depth_ != 0; }

    std::size_t depth() const noexcept { return depth_; }

    [[nodiscard]] const std::byte* take(std::uint32_t hunk) noexcept;

    void submit(std::uint32_t hunk) noexcept;

    void note_miss() noexcept { misses_.fetch_add(1, std::memory_order_relaxed); }

    [[nodiscard]] PrefetchCounters counters() const noexcept;

    [[nodiscard]] bool holds_slot_for_test() const noexcept { return static_cast<bool>(held_); }

    [[nodiscard]] std::size_t slot_census_for_test() const noexcept {
        return chan_.census().total();
    }

private:
    [[nodiscard]] bool sprinting_() const noexcept {
        return (next_ - tracking_) < static_cast<std::uint32_t>(kPrefetchMinDepth);
    }

    void reset_consumer_() noexcept;

    void rest_(int ms, std::uint32_t want, bool watch_free) noexcept;
    void release_media_() noexcept;

    void take_attaches_() noexcept;

    [[nodiscard]] bool reap_leases_() noexcept;

    [[nodiscard]] Ex<void> hand_over_(ChdMedia&& media) noexcept;
    void kick_worker_() noexcept;
    std::size_t evict_(std::uint32_t current) noexcept;

    xthread::WakeFlag wake_{};

    Chan chan_{wake_, xthread::Polled{}};
    std::atomic<std::uint32_t> want_hunk_{kNoPrefetchHunk};
    std::atomic<bool> park_req_{false};
    std::atomic<bool> parked_{true};

    xthread::Inbox<ChdAttach, kChdAttachDepth> attaches_{wake_};
    std::atomic<std::uint32_t> attaches_taken_{0};
    xthread::SlotPool<ChdMedia, kChdMediaLeases> media_{};

    std::atomic<std::uint32_t> hits_{0}, misses_{0}, decodes_{0};
    std::atomic<std::uint32_t> decode_us_{0}, evictions_{0};
    std::atomic<std::uint32_t> seeks_{0}, errors_{0};

    using Lease = xthread::SlotPool<ChdMedia, kChdMediaLeases>::Lease;

    struct Commander {
        TASTY_SEAT_RESIDENT(Io);
        Lease leases[kChdMediaLeases]{};
        std::uint32_t lease_seq[kChdMediaLeases]{};
        std::uint32_t sent = 0;
    };

    Commander cmd_{};

    std::unique_ptr<IChdSource> src_{};
    std::uint32_t hunk_count_ = 0;
    std::uint32_t forward_span_ = 0;
    std::uint32_t worker_hunk_bytes_ = 0;
    std::uint32_t taken_ = 0;
    std::uint32_t next_ = 0;
    std::uint32_t tracking_ = kNoPrefetchHunk;
    bool disabled_ = false;

    bool rests_ = false;
    bool rest_parked_ = false;
    bool watch_free_ = false;
    int rest_ms_ = 0;
    std::uint32_t want_seen_ = kNoPrefetchHunk;

    Chan::Job held_{};

    std::unique_ptr<std::byte[]> slab_{};
    std::size_t hunk_bytes_ = 0;

    std::size_t depth_ = 0;

    Chan::Loan resident_[kPrefetchMaxDepth]{};

    std::uint32_t resident_hunk_[kPrefetchMaxDepth]{};
    std::size_t resident_count_ = 0;
    std::uint32_t want_ = kNoPrefetchHunk;

    bool poisoned_ = false;
};

}
