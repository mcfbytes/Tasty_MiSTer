// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <climits>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>

#include "infra/error.h"
#include "infra/fixed_str.h"
#include "os/vm_compaction.h"
#include "diag_main.h"
#include "ui_main.h"
#include "reactor/executive.h"
#include "hal/boards_table.h"
#include "svc/input_service.h"
#include "infra/seat.h"

namespace mister::fw {

struct RtSetup;
struct RtEvidence;

enum class RtMode : std::uint8_t { Required, BestEffort };

void pin_current_cpu(os::CpuId cpu, RtSetup& out) noexcept;

[[nodiscard]] Ex<void> verify_cpu_topology(
    const hal::ThreadMap& m,
    long online_cpus,
    RtMode mode,
    RtEvidence& ev
) noexcept;

[[nodiscard]] Ex<void> rt_topology_init(
    const hal::ThreadMap& m,
    RtMode mode,
    RtEvidence& ev
) noexcept;

[[nodiscard]] Ex<void> rt_memory_init(RtMode mode, RtEvidence& ev);

void warn_vm_compaction(const os::VmCompaction& vm, const char* prog, std::FILE* out) noexcept;

inline constexpr std::size_t kStackPageBytes = 4096;

inline constexpr std::size_t kStackPrefaultReserveBytes = 64u * 1024u;

struct StackExtent {
    std::size_t used = 0;
    std::size_t mapped = 0;
    std::uint64_t rlimit = 0;

    [[nodiscard]] constexpr std::size_t bound(bool growable) const noexcept {
        if (growable && rlimit > mapped) {
            return rlimit > static_cast<std::uint64_t>(SIZE_MAX) ? SIZE_MAX
                                                                 : static_cast<std::size_t>(rlimit);
        }
        return mapped;
    }

    [[nodiscard]] constexpr std::size_t span(std::size_t requested, bool growable) const noexcept {
        std::uint64_t limit = bound(growable);
        if (rlimit < limit)
            limit = rlimit;
        const std::uint64_t floor = static_cast<std::uint64_t>(used) + kStackPrefaultReserveBytes;
        if (limit <= floor)
            return 0;
        const std::uint64_t room = limit - floor;
        return room < requested ? static_cast<std::size_t>(room) : requested;
    }
};

void prefault_current_stack(std::size_t requested, RtEvidence& ev) noexcept;

struct SchedInfo {

    std::optional<hal::SchedPolicy> policy{};
    int rt_priority = -1;
    char state = '?';
    FixedStr<31, StrFit::Clip> wchan;
};

[[nodiscard]] Ex<SchedInfo> read_thread_sched(long tid);

[[nodiscard]] constexpr int sched_policy_field(hal::SchedPolicy p) noexcept {
    constexpr int kSchedFifoField = 1;
    return p == hal::SchedPolicy::Fifo ? kSchedFifoField : 0;
}

inline constexpr int kRtWedgeExitStatus = 75;

inline constexpr std::chrono::nanoseconds kRtStopDeadlineNs{std::chrono::seconds{120}};

inline constexpr std::chrono::nanoseconds kIoStopDeadlineNs{std::chrono::seconds{5}};

inline constexpr std::chrono::nanoseconds kRtJoinSliceNs{std::chrono::milliseconds{20}};

[[nodiscard]] Ex<std::uint64_t> read_vm_rss_bytes();

inline constexpr std::size_t kInputWorstCaseFds = svc::kMaxDevices + 1;
static_assert(
    kInputWorstCaseFds > reactor::Executive::kMaxBorrowedFds,
    "item / arch amendment 2026-08-18, RULE 1: the executive's "
    "borrowed-fd table is a FABRIC budget and must stay too small to "
    "hold an evdev device tree. If you widened kMaxBorrowedFds to fit "
    "input again, that is the regression this line exists to catch — "
    "T-INPUT owns the evdev fds (app::InputDecode), not the executive."
);

inline constexpr std::size_t kAppBorrowedFds = 2;

static_assert(
    DiagMain::kSeat != UiMain::kSeat,
    "item: two thread bodies cannot occupy one seat — the second "
    "would take the first's thread-map name, its CPU pin and its "
    "RtEvidence row, and adopt_seat() would write one slot twice."
);
static_assert(
    hal::every_thread_map([](const hal::ThreadMap& m) {
        return hal::seat_of(m, DiagMain::kSeat).policy == hal::SchedPolicy::Other
            && hal::seat_of(m, UiMain::kSeat).policy == hal::SchedPolicy::Other;
    }),
    "item: both in-house bodies BLOCK (proc reads, poll(2), "
    "stdio, filesystem) and must sit on a SCHED_OTHER row — putting "
    "either on a SCHED_FIFO seat puts blocking work on the RT "
    "partition (arch §2)."
);

}
