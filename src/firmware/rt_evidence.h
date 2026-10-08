// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "rt_setup.h"
#include "os/kernel_rt.h"
#include "os/vm_compaction.h"
#include "hal/thread_map.h"
#include "infra/seat.h"

namespace mister::fw {

struct RtEvidence {
    TASTY_SEAT_MEDIATOR(Any, Diag);
    RtSetup mem_lock;
    RtSetup trim_pin;
    RtSetup mmap_pin;

    RtSetup cpu_topology;

    RtSetup cpu_coverage;
    long online_cpus = 0;
    RtSetup main_affinity;

    std::array<RtSetup, hal::kThreadSeats> seat_sched{};
    std::array<RtSetup, hal::kThreadSeats> seat_affinity{};

    RtSetup main_stack_prefault;
    std::size_t main_stack_prefault_bytes = 0;
    std::uint64_t stack_rlimit_bytes = 0;

    RtSetup irq_affinity;

    RtSetup kernel_rt;

    os::KernelRt kernel = os::KernelRt::Unknown;

    os::VmCompaction vm_compaction;

    RtSetup seat_name[hal::kThreadSeats];
};

struct RtEvidenceRow {
    using SeatRows = std::array<RtSetup, hal::kThreadSeats>;
    RtSetup RtEvidence::*field = nullptr;
    SeatRows RtEvidence::*per_seat = nullptr;
    SeatTag seat{};

    constexpr const RtSetup& of(const RtEvidence& ev) const noexcept {
        return per_seat != nullptr ? (ev.*per_seat)[hal::row_index(seat)] : ev.*field;
    }
    constexpr RtSetup& of(RtEvidence& ev) const noexcept {
        return per_seat != nullptr ? (ev.*per_seat)[hal::row_index(seat)] : ev.*field;
    }
    constexpr bool named() const noexcept { return (field != nullptr) != (per_seat != nullptr); }
    friend constexpr bool operator==(const RtEvidenceRow& a, const RtEvidenceRow& b) noexcept {
        if (a.field != b.field || a.per_seat != b.per_seat) return false;
        return a.per_seat == nullptr || a.seat == b.seat;
    }
};
constexpr RtEvidenceRow process_row(RtSetup RtEvidence::*f) noexcept { return {f, nullptr, {}}; }
constexpr RtEvidenceRow sched_row(SeatTag s) noexcept {
    return {nullptr, &RtEvidence::seat_sched, s};
}
constexpr RtEvidenceRow affinity_row(SeatTag s) noexcept {
    return {nullptr, &RtEvidence::seat_affinity, s};
}

inline constexpr std::size_t kRtEvidenceRows = 30;
inline constexpr std::array<RtEvidenceRow, kRtEvidenceRows> kRtEvidenceRowTable{
    process_row(&RtEvidence::mem_lock),
    process_row(&RtEvidence::trim_pin),
    process_row(&RtEvidence::mmap_pin),
    process_row(&RtEvidence::cpu_topology),
    process_row(&RtEvidence::main_affinity),
    affinity_row(SeatTag::Diag),
    affinity_row(SeatTag::Ui),
    sched_row(SeatTag::Diag),
    sched_row(SeatTag::Ui),
    sched_row(SeatTag::Frame),
    affinity_row(SeatTag::Frame),
    sched_row(SeatTag::Input),
    affinity_row(SeatTag::Input),
    sched_row(SeatTag::Prefetch),
    affinity_row(SeatTag::Prefetch),
    sched_row(SeatTag::RT),
    affinity_row(SeatTag::RT),
    process_row(&RtEvidence::kernel_rt),
    process_row(&RtEvidence::main_stack_prefault),
    process_row(&RtEvidence::irq_affinity),
    sched_row(SeatTag::Pcm),
    affinity_row(SeatTag::Pcm),
    sched_row(SeatTag::Io),
    affinity_row(SeatTag::Io),
    sched_row(SeatTag::Capture),
    affinity_row(SeatTag::Capture),
    sched_row(SeatTag::Encode),
    affinity_row(SeatTag::Encode),
    sched_row(SeatTag::RecWrite),
    affinity_row(SeatTag::RecWrite),
};

inline constexpr std::array kRtOkOptionalSeats{SeatTag::Launcher, SeatTag::HdOsd};

consteval bool rt_evidence_rows_named_once() {
    for (std::size_t i = 0; i < kRtEvidenceRows; ++i) {
        if (!kRtEvidenceRowTable[i].named()) return false;
        for (std::size_t j = i + 1; j < kRtEvidenceRows; ++j) {
            if (kRtEvidenceRowTable[i] == kRtEvidenceRowTable[j]) return false;
        }
    }
    return true;
}
static_assert(rt_evidence_rows_named_once(),
              "every rt.ok row must name a distinct RtEvidence member — a null slot was "
              "once dereferenced as UB, a duplicate would render one row twice");

consteval bool rt_evidence_seats_covered() {
    for (std::size_t i = 0; i < hal::kThreadSeats; ++i) {
        const auto s = static_cast<SeatTag>(i + 1);
        bool optional = false;
        for (const SeatTag o : kRtOkOptionalSeats)
            optional = optional || o == s;
        int rows = 0;
        for (const RtEvidenceRow& r : kRtEvidenceRowTable) {
            if (r == sched_row(s) || r == affinity_row(s)) ++rows;
        }
        if (rows != (optional ? 0 : 2)) return false;
    }
    return true;
}
static_assert(rt_evidence_seats_covered(),
              "a seat's sched and affinity rows are both in rt.ok, or the seat is optional");

static_assert(kRtEvidenceRowTable[15] == sched_row(SeatTag::RT));
static_assert(kRtEvidenceRowTable[16] == affinity_row(SeatTag::RT));
static_assert(kRtEvidenceRowTable[17] == process_row(&RtEvidence::kernel_rt));
static_assert(kRtEvidenceRowTable[18] == process_row(&RtEvidence::main_stack_prefault));
static_assert(kRtEvidenceRowTable[19] == process_row(&RtEvidence::irq_affinity));
static_assert(kRtEvidenceRowTable[20] == sched_row(SeatTag::Pcm));
static_assert(kRtEvidenceRowTable[21] == affinity_row(SeatTag::Pcm));
static_assert(kRtEvidenceRowTable[22] == sched_row(SeatTag::Io));
static_assert(kRtEvidenceRowTable[23] == affinity_row(SeatTag::Io));
static_assert(kRtEvidenceRowTable[24] == sched_row(SeatTag::Capture));
static_assert(kRtEvidenceRowTable[29] == affinity_row(SeatTag::RecWrite));

static_assert(kRtEvidenceRows <= 64, "rt.ok is one 64-bit word: a 65th row has no bit");
inline constexpr std::uint64_t kRtEvidenceAllApplied =
    kRtEvidenceRows == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << kRtEvidenceRows) - 1u;

constexpr std::uint64_t rt_evidence_mask(const RtEvidence& ev) noexcept {
    std::uint64_t m = 0;
    for (std::size_t i = 0; i < kRtEvidenceRows; ++i) {
        if (kRtEvidenceRowTable[i].of(ev).applied) m |= (std::uint64_t{1} << i);
    }
    return m;
}

constexpr unsigned rt_first_errno(const RtEvidence& ev) noexcept {
    for (std::size_t i = 0; i < kRtEvidenceRows; ++i) {
        if (const int e = kRtEvidenceRowTable[i].of(ev).err; e != 0)
            return static_cast<unsigned>(e);
    }
    return 0;
}

}  // namespace mister::fw
