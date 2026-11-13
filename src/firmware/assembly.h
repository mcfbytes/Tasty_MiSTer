// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>

#include "diag_main.h"
#include "hal/boards_table.h"
#include "infra/seat.h"
#include "reactor/executive.h"
#include "svc/device_table.h"
#include "ui_main.h"

namespace mister::fw {

inline constexpr int kRtWedgeExitStatus = 75;

inline constexpr std::chrono::nanoseconds kRtStopDeadlineNs{std::chrono::seconds{120}};

inline constexpr std::chrono::nanoseconds kIoStopDeadlineNs{std::chrono::seconds{5}};

inline constexpr std::chrono::nanoseconds kRtJoinSliceNs{std::chrono::milliseconds{20}};

inline constexpr std::size_t kInputWorstCaseFds = svc::DeviceTable::kMaxDevices + 1;
static_assert(
    kInputWorstCaseFds > reactor::Executive::kMaxBorrowedFds,
    "The executive's "
    "borrowed-fd table is a FABRIC budget and must stay too small to "
    "hold an evdev device tree. If you widened kMaxBorrowedFds to fit "
    "input again, that is the regression this line exists to catch — "
    "T-INPUT owns the evdev fds (app::InputDecode), not the executive."
);

inline constexpr std::size_t kAppBorrowedFds = 1;

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
    "partition."
);

}
