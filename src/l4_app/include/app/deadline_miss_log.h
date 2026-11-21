// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <type_traits>

#include "app/log_lane.h"
#include "app/log_rec.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "reactor/deadline_miss.h"
#include "reactor/deadline_miss_sink.h"

namespace mister::app {

class DeadlineMissLog final : public reactor::IDeadlineMissSink {
    TASTY_SEAT_RESIDENT(RT);

public:
    constexpr explicit DeadlineMissLog(LogLane::Producer lane) noexcept : lane_(lane) {}

    void on(const reactor::DeadlineMiss& miss, std::chrono::nanoseconds wake) noexcept override {
        TASTY_SEAT_BODY(DeadlineMissLog);
        lane_.push(LogRec::DeadlineMiss{.over = miss.over, .slot = miss.slot}, wake);
    }

private:
    LogLane::Producer lane_;
};

static_assert(
    std::is_trivially_destructible_v<DeadlineMissLog>,
    "a process static: no destructor runs at exit"
);

}
