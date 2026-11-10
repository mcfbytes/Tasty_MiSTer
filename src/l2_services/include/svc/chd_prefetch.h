// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "infra/counter.h"
#include "infra/error.h"
#include "infra/inbox.h"
#include "infra/indexed_array.h"
#include "infra/park_timeout.h"
#include "infra/polled.h"
#include "infra/pool_slot.h"
#include "infra/rt_stats.h"
#include "infra/slot_pool.h"
#include "infra/loan_channel.h"
#include "infra/spsc_ring.h"
#include "infra/wake_flag.h"
#include "svc/chd_geometry.h"
#include "svc/chd_source.h"
#include "infra/seat.h"

namespace mister::svc {

struct PrefetchSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);
    HunkIndex hunk{HunkIndex::none()};
};

static_assert(sizeof(PrefetchSlot) == 4, "item: the slot names a hunk; the INDEX names the buffer");

inline constexpr HunkCount kPrefetchMinHunks{8};
inline constexpr HunkCount kPrefetchMaxHunks{64};

inline constexpr std::size_t kPrefetchMinDepth = kPrefetchMinHunks.v;
inline constexpr std::size_t kPrefetchMaxDepth = kPrefetchMaxHunks.v;
inline constexpr std::size_t kPrefetchBudgetBytes = 16u * 1024u * 1024u;

inline constexpr HunkCount kPrefetchLookBehind{16};
inline constexpr HunkCount kPrefetchForwardSpan{16};

inline constexpr HunkCount kPrefetchRingFloorHunks = kPrefetchLookBehind + kPrefetchForwardSpan;
inline constexpr HunkCount kOneHunk{1};

inline constexpr infra::ParkTimeout kPrefetchTopUpMs{40};

static_assert(
    (kPrefetchMaxDepth & (kPrefetchMaxDepth - 1)) == 0,
    "xthread::SpscRing requires a power-of-two capacity"
);
static_assert(kPrefetchMinDepth <= kPrefetchMaxDepth, "the floor cannot exceed the ceiling");
static_assert(
    kPrefetchForwardSpan >= kPrefetchMinHunks,
    "the speculation budget cannot be tighter than the SPRINT floor, or the sprint "
    "goes idle before it satisfies the three measured floors item pins it to"
);
static_assert(
    kPrefetchRingFloorHunks <= kPrefetchMaxHunks,
    "both budgets are carved out of the ring at its widest"
);
static_assert(
    kPrefetchLookBehind < kPrefetchRingFloorHunks,
    "the look-behind is carved OUT of the ring at every derivable depth: it cannot "
    "consume the whole thing even at the floor, or the producer has no runway at all"
);

constexpr std::optional<HunkCount> derive_depth(
    std::size_t budget,
    proto::FileSize hunk_bytes
) noexcept {
    if (hunk_bytes.empty())
        return std::nullopt;
    const HunkCount derived = HunkCount::of_slots(hunk_bytes.blocks_in(budget));
    if (derived < kPrefetchRingFloorHunks)
        return std::nullopt;
    return derived < kPrefetchMaxHunks ? derived : kPrefetchMaxHunks;
}

static_assert(
    kPrefetchRingFloorHunks - kPrefetchLookBehind >= kOneHunk,
    "at the floor: the producer must keep at least one hunk of runway"
);
static_assert(
    kPrefetchRingFloorHunks <= kPrefetchMaxHunks,
    "the floor cannot exceed the ring's compile-time capacity"
);
static_assert(
    kPrefetchMaxHunks - kPrefetchLookBehind >= kPrefetchRingFloorHunks - kPrefetchLookBehind,
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

    ChdGeometry geo{};
    HunkCount forward_span{};
};

struct ChdAttach {
    TASTY_SEAT_EXEMPT(component);
    xthread::PoolSlot lease{};
};

enum class PrefetchState : std::uint32_t {
    Closed,
    Parked,
    Running,
};

struct PrefetchCounters {
    std::uint32_t hits = 0;
    std::uint32_t misses = 0;
    std::uint32_t decodes = 0;
    std::uint32_t seeks = 0;
    std::uint32_t drops = 0;
    std::uint32_t errors = 0;
    PrefetchState state = PrefetchState::Closed;

    std::uint32_t decode_us = 0;
    std::uint32_t evictions = 0;
};

class ChdPrefetch {

    struct HandoverSeq {
        std::uint32_t v = 0;

        [[nodiscard]] constexpr HandoverSeq next() const noexcept { return HandoverSeq{v + 1}; }

        [[nodiscard]] constexpr bool reached(HandoverSeq target) const noexcept {
            return static_cast<std::int32_t>(v - target.v) >= 0;
        }

        friend constexpr bool operator==(HandoverSeq, HandoverSeq) = default;
    };

    static_assert(
        std::atomic<HandoverSeq>::is_always_lock_free,
        "the handover count crosses T-IO and T-PREFETCH: its atomic never takes a lock"
    );

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

    [[nodiscard]] infra::ParkTimeout park_ms() const noexcept { return rest_ms_; }

    void release() noexcept;

    void park_now() noexcept;

    void request_park() noexcept;

    [[nodiscard]] bool park_acked() noexcept;

    [[nodiscard]] bool settle_park() noexcept;

    [[nodiscard]] Ex<void> attach(std::unique_ptr<IChdSource> src, ChdGeometry geo) noexcept;

    bool parked() const noexcept { return parked_.load(std::memory_order_acquire); }

    template <class Flag>
    [[nodiscard]] static bool park_due(const Flag& parked, const Flag& park_req) noexcept {
        return !parked.load(std::memory_order_acquire) && park_req.load(std::memory_order_acquire);
    }

    bool attached() const noexcept { return depth_ != HunkCount{}; }

    HunkCount depth() const noexcept { return depth_; }

    [[nodiscard]] std::span<const std::byte> take(HunkIndex hunk) noexcept;

    void note_miss() noexcept { misses_.add(); }

    [[nodiscard]] PrefetchState state() const noexcept;
    [[nodiscard]] PrefetchCounters counters() const noexcept;

    [[nodiscard]] bool holds_slot_for_test() const noexcept { return static_cast<bool>(held_); }

    [[nodiscard]] std::size_t slot_census_for_test() const noexcept {
        return chan_.census().total();
    }

private:
    [[nodiscard]] bool sprinting_() const noexcept {
        return (next_ - tracking_) < kPrefetchMinHunks;
    }

    void reset_consumer_() noexcept;

    void rest_(infra::ParkTimeout ms, HunkIndex want, bool watch_free) noexcept;
    void release_media_() noexcept;

    void take_attaches_() noexcept;

    [[nodiscard]] bool reap_leases_() noexcept;

    [[nodiscard]] Ex<void> hand_over_(ChdMedia&& media) noexcept;
    void kick_worker_() noexcept;
    std::size_t evict_(HunkIndex current) noexcept;

    [[nodiscard]] std::span<std::byte> slot_bytes_(xthread::PoolSlot slot) const noexcept;

    xthread::WakeFlag wake_{};

    Chan chan_{wake_, xthread::Polled{}};
    std::atomic<HunkIndex> want_hunk_{HunkIndex::none()};
    std::atomic<bool> park_req_{false};
    std::atomic<bool> parked_{true};

    xthread::Inbox<ChdAttach, kChdAttachDepth> attaches_{wake_};

    std::atomic<HandoverSeq> attaches_taken_{HandoverSeq{}};
    xthread::SlotPool<ChdMedia, kChdMediaLeases, kSeatMediatorB> media_{};

    xthread::Counter hits_, misses_, decodes_, decode_us_, evictions_, seeks_, errors_;

    using Lease = xthread::SlotPool<ChdMedia, kChdMediaLeases, kSeatMediatorB>::Lease;

    struct HeldLease {
        TASTY_SEAT_RESIDENT(Io);
        Lease lease{};
        HandoverSeq seq{};

        ~HeldLease() { (void)lease.release(); }
    };

    struct Commander {
        TASTY_SEAT_RESIDENT(Io);
        infra::IndexedArray<HeldLease, kChdMediaLeases, xthread::PoolSlot> leases{};
        HandoverSeq sent{};
    };

    static_assert(
        decltype(media_)::owner_seat() == Commander::kSeatDecl,
        "the seat that holds the leases is the seat the pool checks"
    );
    Commander cmd_{};

    std::unique_ptr<IChdSource> src_{};
    ChdGeometry geo_{};
    HunkCount forward_span_{};
    HandoverSeq taken_{};
    HunkIndex next_{};
    HunkIndex tracking_{HunkIndex::none()};
    bool disabled_ = false;

    bool rests_ = false;
    bool rest_parked_ = false;
    bool watch_free_ = false;
    infra::ParkTimeout rest_ms_ = infra::kNoWait;
    HunkIndex want_seen_{HunkIndex::none()};

    Chan::Job held_{};

    std::unique_ptr<std::byte[]> slab_{};
    std::size_t hunk_extent_{};

    HunkCount depth_{};

    Chan::Loan resident_[kPrefetchMaxDepth]{};

    HunkIndex resident_hunk_[kPrefetchMaxDepth]{};
    std::size_t resident_count_ = 0;
    HunkIndex want_{HunkIndex::none()};

    bool poisoned_ = false;
};

}
