// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <climits>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "infra/error.h"
#include "os/kernel_rt.h"
#include "os/vm_compaction.h"
#include "stack_extent.h"
#include "rt_setup.h"
#include "diag_main.h"
#include "ui_main.h"
#include "reactor/executive.h"
#include "hal/boards_table.h"
#include "svc/device_table.h"
#include "infra/seat.h"

namespace mister::fw {

enum class RtMode : std::uint8_t { Required, BestEffort };

struct TopologyOutcome {
    long online = 0;
    RtSetup coverage{};
    RtSetup topology{};
    Ex<void> verdict{};
};

[[nodiscard]] RtSetup pin_current_cpu(os::CpuId cpu) noexcept;

[[nodiscard]] RtSetup confirm_current_cpu(os::CpuId cpu) noexcept;

[[nodiscard]] TopologyOutcome verify_cpu_topology(
    const hal::ThreadMap& m,
    long online_cpus,
    RtMode mode
) noexcept;

[[nodiscard]] TopologyOutcome rt_topology_init(const hal::ThreadMap& m, RtMode mode) noexcept;

struct MemoryPolicyOutcome {
    os::KernelRt kernel = os::KernelRt::Unknown;
    RtSetup kernel_rt{};
    RtSetup trim_pin{};
    RtSetup mmap_pin{};
    RtSetup mem_lock{};
    os::VmCompaction vm{};
    Ex<void> verdict{};
};

[[nodiscard]] MemoryPolicyOutcome rt_memory_init(RtMode mode);

void warn_vm_compaction(const os::VmCompaction& vm, const char* prog, std::FILE* out) noexcept;

struct StackPrefault {
    RtSetup outcome{};
    std::size_t bytes = 0;
    std::uint64_t rlimit = 0;
};

[[nodiscard]] StackPrefault prefault_current_stack(std::size_t requested) noexcept;

inline constexpr int kRtWedgeExitStatus = 75;

inline constexpr std::chrono::nanoseconds kRtStopDeadlineNs{std::chrono::seconds{120}};

inline constexpr std::chrono::nanoseconds kIoStopDeadlineNs{std::chrono::seconds{5}};

inline constexpr std::chrono::nanoseconds kRtJoinSliceNs{std::chrono::milliseconds{20}};

inline constexpr std::size_t kInputWorstCaseFds = svc::DeviceTable::kMaxDevices + 1;
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
