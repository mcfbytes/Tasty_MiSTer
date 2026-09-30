// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>

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

void pin_current_cpu(int cpu, RtSetup& out) noexcept;

Ex<void> verify_cpu_topology(const hal::ThreadMap& m, long online_cpus, RtMode mode,
                             RtEvidence& ev) noexcept;

Ex<void> rt_topology_init(const hal::ThreadMap& m, RtMode mode, RtEvidence& ev) noexcept;

Ex<void> rt_memory_init(RtMode mode, RtEvidence& ev);

void warn_vm_compaction(const os::VmCompaction& vm, const char* prog, std::FILE* out) noexcept;

inline constexpr std::size_t kStackPageBytes = 4096;

inline constexpr std::size_t kStackPrefaultReserveBytes = 64u * 1024u;

constexpr std::size_t prefault_mapped_bound(std::size_t mapped, std::uint64_t rlimit_cur,
                                            bool growable) noexcept {
    if (growable && rlimit_cur > mapped) {
        return rlimit_cur > static_cast<std::uint64_t>(SIZE_MAX)
                   ? SIZE_MAX
                   : static_cast<std::size_t>(rlimit_cur);
    }
    return mapped;
}
constexpr std::size_t stack_prefault_span(std::size_t requested, std::size_t used,
                                          std::size_t mapped, std::uint64_t rlimit_cur) noexcept {
    std::uint64_t limit = mapped;
    if (rlimit_cur < limit) limit = rlimit_cur;
    const std::uint64_t floor = static_cast<std::uint64_t>(used) + kStackPrefaultReserveBytes;
    if (limit <= floor) return 0;
    const std::uint64_t room = limit - floor;
    return room < requested ? static_cast<std::size_t>(room) : requested;
}

void prefault_current_stack(std::size_t requested, RtEvidence& ev) noexcept;

struct SchedInfo {
    int policy = -1;
    int rt_priority = -1;
    char state = '?';
    FixedStr<31, StrFit::Clip> wchan;
};
Ex<SchedInfo> read_thread_sched(long tid);

inline constexpr int kRtWedgeExitStatus = 75;

inline constexpr std::int64_t kRtStopDeadlineNs = 120'000'000'000;

inline constexpr std::int64_t kIoStopDeadlineNs = 5'000'000'000;

inline constexpr std::int64_t kRtJoinSliceNs = 20'000'000;

Ex<std::uint64_t> read_vm_rss_bytes();

inline constexpr std::size_t kInputWorstCaseFds = svc::kMaxDevices + 1;
static_assert(kInputWorstCaseFds > reactor::Executive::kMaxBorrowedFds,
              "item / arch amendment 2026-08-18, RULE 1: the executive's "
              "borrowed-fd table is a FABRIC budget and must stay too small to "
              "hold an evdev device tree. If you widened kMaxBorrowedFds to fit "
              "input again, that is the regression this line exists to catch — "
              "T-INPUT owns the evdev fds (app::InputDecode), not the executive.");

inline constexpr std::size_t kAppBorrowedFds = 2;

static_assert(DiagMain::kSeat != UiMain::kSeat,
              "item: two thread bodies cannot occupy one seat — the second "
              "would take the first's thread-map name, its CPU pin and its "
              "RtEvidence row, and adopt_seat() would write one slot twice.");
static_assert(hal::every_thread_map([](const hal::ThreadMap& m) {
                  return hal::seat_of(m, DiagMain::kSeat).policy == hal::SchedPolicy::Other &&
                         hal::seat_of(m, UiMain::kSeat).policy == hal::SchedPolicy::Other;
              }),
              "item: both in-house bodies BLOCK (proc reads, poll(2), "
              "stdio, filesystem) and must sit on a SCHED_OTHER row — putting "
              "either on a SCHED_FIFO seat puts blocking work on the RT "
              "partition (arch §2).");

}  // namespace mister::fw
