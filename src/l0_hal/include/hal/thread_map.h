// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "hal/stack_bytes.h"
#include "hal/types.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/seat_role.h"
#include "os/types.h"

namespace mister::hal {

enum class SpawnKind : std::uint8_t { PromoteInPlace, Create };

enum class DdrMapping : std::uint8_t { None, Owner };

enum class PauseEdge : std::uint8_t { Ask, Receipt };

inline constexpr std::size_t kThreadSeats = kSeatTagCount - 2;
static_assert(static_cast<std::size_t>(SeatTag::Main) == kSeatTagCount - 1, "Main is the last tag");

struct SeatPolicy {
    SeatTag seat;
    SchedPolicy policy;
    int prio;
    StackBytes stack_bytes;
    SpawnKind spawn;
    DdrMapping ddr = DdrMapping::None;
    PauseEdge pause_at = PauseEdge::Ask;
    friend constexpr bool operator==(const SeatPolicy&, const SeatPolicy&) = default;
};

using SeatPolicyTable = std::array<SeatPolicy, kThreadSeats>;

struct SeatPlacement {
    SeatTag seat;
    os::CpuId cpu;
};

using PlacementTable = std::array<SeatPlacement, kThreadSeats>;

struct ThreadRole : SeatPolicy {
    TASTY_SEAT_EXEMPT(const_shared);
    os::CpuId cpu;
    friend constexpr bool operator==(const ThreadRole&, const ThreadRole&) = default;
};

using ThreadMap = std::array<ThreadRole, kThreadSeats>;

inline constexpr std::size_t kCommNameMax = 15;

inline constexpr std::size_t kCommRoleMax = 10;

struct CommName {
    std::array<char, kCommNameMax + 1> text{};
};

constexpr CommName comm_name(std::string_view prefix, std::string_view role) noexcept {
    CommName out;
    if (role.size() > kCommNameMax)
        role = role.substr(0, kCommNameMax);
    const std::size_t room = kCommNameMax - role.size();
    const std::size_t avail = room > 0 ? room - 1 : 0;
    const std::size_t keep = prefix.size() < avail ? prefix.size() : avail;
    std::size_t n = 0;
    for (std::size_t i = 0; i < keep; ++i)
        out.text[n++] = prefix[i];
    if (keep > 0)
        out.text[n++] = ':';
    for (const char c : role)
        out.text[n++] = c;
    return out;
}

inline constexpr StackBytes kFifoStackBytes = StackBytes::kib(256);

inline constexpr StackBytes kOtherStackBytes = StackBytes::mib(8);

inline constexpr StackBytes kRecorderStackBytes = StackBytes::kib(256);

inline constexpr StackBytes kLauncherStackBytes = StackBytes::kib(256);

inline constexpr StackBytes kHdOsdStackBytes = StackBytes::mib(2);

inline constexpr StackBytes kConnectStackBytes = StackBytes::kib(512);

inline constexpr StackBytes kNicStackBytes = StackBytes::kib(256);

inline constexpr SeatPolicyTable kSeatPolicyTable{{
    {.seat = SeatTag::RT,
     .policy = SchedPolicy::Fifo,
     .prio = 40,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::Frame,
     .policy = SchedPolicy::Fifo,
     .prio = 35,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::Input,
     .policy = SchedPolicy::Fifo,
     .prio = 25,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::Pcm,
     .policy = SchedPolicy::Fifo,
     .prio = 22,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create,
     .ddr = DdrMapping::Owner},
    {.seat = SeatTag::Prefetch,
     .policy = SchedPolicy::Fifo,
     .prio = 20,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::Diag,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kOtherStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::Ui,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kOtherStackBytes,
     .spawn = SpawnKind::Create,
     .ddr = DdrMapping::Owner},
    {.seat = SeatTag::Io,
     .policy = SchedPolicy::Fifo,
     .prio = 23,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create,
     .ddr = DdrMapping::Owner,
     .pause_at = PauseEdge::Receipt},
    {.seat = SeatTag::Capture,
     .policy = SchedPolicy::Fifo,
     .prio = 21,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::Encode,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kRecorderStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::RecWrite,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kRecorderStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::Launcher,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kLauncherStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::HdOsd,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kHdOsdStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::Connect,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kConnectStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::Nic,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kNicStackBytes,
     .spawn = SpawnKind::Create},
}};

inline constexpr PlacementTable kDe10Placement{{
    {.seat = SeatTag::RT, .cpu = os::CpuId{1}},
    {.seat = SeatTag::Frame, .cpu = os::CpuId{1}},
    {.seat = SeatTag::Input, .cpu = os::CpuId{0}},
    {.seat = SeatTag::Pcm, .cpu = os::CpuId{0}},
    {.seat = SeatTag::Prefetch, .cpu = os::CpuId{0}},
    {.seat = SeatTag::Diag, .cpu = os::CpuId{0}},
    {.seat = SeatTag::Ui, .cpu = os::CpuId{0}},
    {.seat = SeatTag::Io, .cpu = os::CpuId{0}},
    {.seat = SeatTag::Capture, .cpu = os::CpuId{0}},
    {.seat = SeatTag::Encode, .cpu = os::CpuId{0}},
    {.seat = SeatTag::RecWrite, .cpu = os::CpuId{0}},
    {.seat = SeatTag::Launcher, .cpu = os::CpuId{0}},
    {.seat = SeatTag::HdOsd, .cpu = os::CpuId{0}},
    {.seat = SeatTag::Connect, .cpu = os::CpuId{0}},
    {.seat = SeatTag::Nic, .cpu = os::CpuId{0}},
}};

constexpr bool has_row(SeatTag tag) noexcept {
    return tag != SeatTag::Unbound && tag != SeatTag::Main;
}

constexpr std::size_t row_index(SeatTag tag) noexcept {
    if (!has_row(tag))
        __builtin_trap();
    return static_cast<std::size_t>(tag) - 1;
}

template <class Row>
constexpr const Row& seat_of(const std::array<Row, kThreadSeats>& m, SeatTag tag) noexcept {
    return m[row_index(tag)];
}

constexpr std::size_t name_len(const char* s) noexcept {
    std::size_t n = 0;
    while (s[n] != '\0')
        ++n;
    return n;
}

template <class Row>
constexpr bool well_formed(const std::array<Row, kThreadSeats>& m) noexcept {
    for (std::size_t i = 0; i < m.size(); ++i) {
        if (static_cast<std::size_t>(m[i].seat) != i + 1)
            return false;
    }
    return true;
}

constexpr ThreadMap join_threads(
    const SeatPolicyTable& seats,
    const PlacementTable& placed
) noexcept {
    ThreadMap m{};
    for (const SeatPolicy& p : seats) {
        const SeatPlacement& at = seat_of(placed, p.seat);
        if (at.seat != p.seat)
            __builtin_trap();
        m[row_index(p.seat)] = ThreadRole{p, at.cpu};
    }
    return m;
}

inline constexpr ThreadMap kDe10ThreadMap = join_threads(kSeatPolicyTable, kDe10Placement);

inline constexpr PlacementTable kDe25Placement{{
    {.seat = SeatTag::RT, .cpu = os::CpuId{1}},
    {.seat = SeatTag::Frame, .cpu = os::CpuId{1}},
    {.seat = SeatTag::Input, .cpu = os::CpuId{2}},
    {.seat = SeatTag::Pcm, .cpu = os::CpuId{2}},
    {.seat = SeatTag::Prefetch, .cpu = os::CpuId{2}},
    {.seat = SeatTag::Diag, .cpu = os::CpuId{0}},
    {.seat = SeatTag::Ui, .cpu = os::CpuId{3}},
    {.seat = SeatTag::Io, .cpu = os::CpuId{2}},
    {.seat = SeatTag::Capture, .cpu = os::CpuId{2}},
    {.seat = SeatTag::Encode, .cpu = os::CpuId{3}},
    {.seat = SeatTag::RecWrite, .cpu = os::CpuId{0}},
    {.seat = SeatTag::Launcher, .cpu = os::CpuId{0}},
    {.seat = SeatTag::HdOsd, .cpu = os::CpuId{3}},
    {.seat = SeatTag::Connect, .cpu = os::CpuId{0}},
    {.seat = SeatTag::Nic, .cpu = os::CpuId{0}},
}};

inline constexpr ThreadMap kDe25ThreadMap = join_threads(kSeatPolicyTable, kDe25Placement);

constexpr bool carries_seat_table(
    const ThreadMap& m,
    const SeatPolicyTable& seats = kSeatPolicyTable
) noexcept {
    for (const ThreadRole& r : m) {
        const SeatPolicy& half = r;
        if (half != seat_of(seats, r.seat))
            return false;
    }
    return true;
}

constexpr bool rt_holds_highest_fifo_prio(const SeatPolicyTable& m) noexcept {
    const SeatPolicy& rt = seat_of(m, SeatTag::RT);
    if (rt.policy != SchedPolicy::Fifo)
        return false;
    for (const SeatPolicy& r : m) {
        if (r.seat == SeatTag::RT)
            continue;
        if (r.policy == SchedPolicy::Fifo && r.prio >= rt.prio)
            return false;
    }
    return true;
}

inline constexpr std::array kFifoOrder{
    SeatTag::RT,
    SeatTag::Frame,
    SeatTag::Input,
    SeatTag::Io,
    SeatTag::Pcm,
    SeatTag::Capture,
    SeatTag::Prefetch
};

inline constexpr std::array kRtCpuSeats{SeatTag::RT, SeatTag::Frame};

constexpr bool fifo_order_holds(
    const SeatPolicyTable& m,
    std::span<const SeatTag> order = kFifoOrder
) noexcept {
    std::size_t fifo_rows = 0;
    for (const SeatPolicy& r : m) {
        if (r.policy == SchedPolicy::Fifo)
            ++fifo_rows;
    }
    if (fifo_rows != order.size())
        return false;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const SeatPolicy& r = seat_of(m, order[i]);
        if (r.policy != SchedPolicy::Fifo)
            return false;
        if (i > 0 && r.prio >= seat_of(m, order[i - 1]).prio)
            return false;
    }
    return true;
}

constexpr bool rt_cpu_reserved(
    const ThreadMap& m,
    std::span<const SeatTag> allowed = kRtCpuSeats
) noexcept {
    const os::CpuId rt = seat_of(m, SeatTag::RT).cpu;
    for (const ThreadRole& r : m) {
        if (r.cpu != rt)
            continue;
        bool listed = false;
        for (const SeatTag t : allowed)
            listed = listed || t == r.seat;
        if (!listed)
            return false;
    }
    return true;
}

inline constexpr std::array kDdrMappingOwners{SeatTag::Pcm, SeatTag::Ui, SeatTag::Io};

constexpr bool ddr_owners_are(
    const SeatPolicyTable& m,
    std::span<const SeatTag> owners = kDdrMappingOwners
) noexcept {
    for (const SeatPolicy& r : m) {
        bool listed = false;
        for (const SeatTag t : owners)
            listed = listed || t == r.seat;
        if ((r.ddr == DdrMapping::Owner) != listed)
            return false;
    }
    return true;
}

inline constexpr std::array kReceiptPauseSeats{SeatTag::Io};

constexpr bool pause_edges_are(
    const SeatPolicyTable& m,
    std::span<const SeatTag> receipt = kReceiptPauseSeats
) noexcept {
    for (const SeatPolicy& r : m) {
        bool listed = false;
        for (const SeatTag t : receipt)
            listed = listed || t == r.seat;
        if ((r.pause_at == PauseEdge::Receipt) != listed)
            return false;
    }
    return true;
}

constexpr bool policy_prio_consistent(const SeatPolicyTable& m) noexcept {
    for (const SeatPolicy& r : m) {
        if (r.policy == SchedPolicy::Fifo) {
            if (r.prio < 1 || r.prio > 99)
                return false;
        } else if (r.prio != 0) {
            return false;
        }
    }
    return true;
}

constexpr bool spawn_stack_consistent(const SeatPolicyTable& m) noexcept {
    for (const SeatPolicy& r : m) {
        if (r.spawn == SpawnKind::Create && r.stack_bytes.is_main())
            return false;
        if (r.spawn == SpawnKind::PromoteInPlace && !r.stack_bytes.is_main())
            return false;
    }
    return true;
}

constexpr SeatTag highest_fifo_seat(const SeatPolicyTable& m) noexcept {
    const SeatPolicy* best = nullptr;
    for (const SeatPolicy& r : m) {
        if (r.policy != SchedPolicy::Fifo)
            continue;
        if (best == nullptr || r.prio > best->prio)
            best = &r;
    }
    return best != nullptr ? best->seat : m[0].seat;
}

constexpr bool names_fit_comm(const std::array<SeatRoleRow, kSeatTagCount>& rows) noexcept {
    for (const SeatRoleRow& r : rows) {
        if (r.role == nullptr || name_len(r.role) == 0)
            return false;
        if (name_len(r.role) > kCommRoleMax)
            return false;
    }
    return true;
}

static_assert(
    well_formed(kSeatPolicyTable) && well_formed(kDe10Placement),
    "The seat table's and the DE10 placement's rows must be in SeatTag order, one row "
    "per real seat. seat_of() indexes by row_index and the evidence record does too; "
    "a reordered or duplicated row would silently hand a call site the wrong seat's "
    "policy, priority and CPU."
);

static_assert(
    rt_holds_highest_fifo_prio(kSeatPolicyTable),
    "T-RT must hold the strictly "
    "highest SCHED_FIFO priority of any seat. If you raised another "
    "seat to or above T-RT, that seat can now preempt the sole GPO "
    "owner mid-SPI-transaction — the gpo_copy shadow hazard by its "
    "real name."
);

static_assert(
    fifo_order_holds(kSeatPolicyTable),
    "Exactly the kFifoOrder seats are SCHED_FIFO, at strictly "
    "falling priority (T-RT, T-FRAME, T-INPUT, T-IO, T-PCM, T-CAPTURE, T-PREFETCH), and "
    "every other seat is SCHED_OTHER. Deadline-monotonic: each edge is a deadline, and "
    "inverting one queues the shorter deadline behind the longer. A tie is the "
    "kernel's run-queue accident; a seat moved to SCHED_OTHER loses the bounded wake it "
    "exists to buy. Change the order and the row together, never the number alone."
);

static_assert(
    ddr_owners_are(kSeatPolicyTable),
    "the seat table's DdrMapping::Owner rows are exactly kDdrMappingOwners: a late owner "
    "refuses the program, so an unmarked seat holding a DDR mapping would be programmed "
    "under, and a marked seat that never parks would wedge every switch."
);

static_assert(
    pause_edges_are(kSeatPolicyTable),
    "the seat table's PauseEdge::Receipt rows are exactly kReceiptPauseSeats: a seat that "
    "pauses at the ask before T-RT queued the teardown's jobs drains nothing, and a seat "
    "that waits for a receipt after the jobs would hold every switch to its deadline."
);

static_assert(
    policy_prio_consistent(kSeatPolicyTable),
    "`policy` and `prio` are ONE decision. A "
    "SchedPolicy::Fifo row must carry a POSIX priority in 1..99 and "
    "a SchedPolicy::Other row must carry 0. Both `{Fifo, 0}` and "
    "`{Fifo, 120}` used to compile and then fail at runtime as an "
    "EINVAL from pthread_attr_setschedparam, taking spawn_seat's "
    "rung-2 degrade with no witness but an evidence row nobody "
    "reads. If you meant to move a seat off SCHED_FIFO, change BOTH "
    "fields and re-read the SCHED_FIFO ordering rule first."
);

static_assert(
    spawn_stack_consistent(kSeatPolicyTable),
    "`spawn` and `stack_bytes` are ONE decision. A "
    "SpawnKind::Create row with stack_bytes == 0 is an EINVAL from "
    "pthread_attr_setstacksize that spawn_seat's rung-2 degrade "
    "swallows with no witness (the attr is discarded and the seat is "
    "born on glibc's default stack, unpinned by this table); a "
    "PromoteInPlace row with a stack owns memory nothing allocates. "
    "Change BOTH fields or neither."
);

static_assert(
    seat_of(kSeatPolicyTable, SeatTag::RT).spawn == SpawnKind::Create,
    "pass ST (c): T-RT is a CREATED thread on its own row-declared "
    "stack, spawned LAST, and ::main is the supervisor that joins it "
    "under a deadline. A PromoteInPlace T-RT IS main, and a thread "
    "cannot time-join itself — a wedged executive would then have no "
    "witness but a silent hang."
);

static_assert(
    names_fit_comm(kSeatRoles),
    "Every seat's comm ROLE must be non-empty and <= 15 chars, "
    "because pthread_setname_np takes 16 bytes INCLUDING the NUL "
    "(glibc returns ERANGE; the kernel truncates). A role that does "
    "not survive is a /proc witness that no longer matches this "
    "table, which is the only channel RT engagement is verified on."
);

constexpr os::CpuId max_cpu(const ThreadMap& m) noexcept {
    os::CpuId hi{0};
    for (const ThreadRole& r : m) {
        if (r.cpu > hi)
            hi = r.cpu;
    }
    return hi;
}

constexpr bool cpus_within(const ThreadMap& m, std::size_t online_cpus) noexcept {
    if (online_cpus == 0)
        return false;
    for (const ThreadRole& r : m) {
        if (!r.cpu.below(online_cpus))
            return false;
    }
    return true;
}

constexpr bool main_off_rt_cpu(const ThreadMap& m, os::CpuId main_cpu) noexcept {
    if (main_cpu == seat_of(m, SeatTag::RT).cpu)
        return false;
    for (const ThreadRole& r : m) {
        if (r.cpu == main_cpu)
            return true;
    }
    return false;
}

void adopt_placement(const ThreadMap& m, SeatTag seat) noexcept;

[[nodiscard]] Ex<void> widen_self_affinity() noexcept;

[[nodiscard]] Ex<void> restore_self_affinity() noexcept;

}
