// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <ctime>
#include <optional>

#include "infra/period_grid.h"
#include "infra/seat.h"
#include "os/timespec.h"
#include "reactor/tick.h"
#include "reactor/types.h"

namespace mister::reactor {

class TickDeadline {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::chrono::nanoseconds kPeriod{kTickNs};

    using Periods = infra::PeriodGrid<MonoInstant>::Periods;

    struct Due {
        MonoInstant latest;
        Periods periods;
    };

    [[nodiscard]] static TickDeadline after(MonoInstant now) noexcept;
    [[nodiscard]] static TickDeadline held() noexcept;

    [[nodiscard]] std::optional<Due> take_due(MonoInstant now) noexcept;

    [[nodiscard]] std::optional<timespec> timeout(MonoInstant now) const noexcept;

private:
    explicit TickDeadline(std::optional<MonoInstant> next) noexcept : grid_(kPeriod), next_(next) {}

    infra::PeriodGrid<MonoInstant> grid_;
    std::optional<MonoInstant> next_;
};

inline TickDeadline TickDeadline::after(MonoInstant now) noexcept {
    return TickDeadline{now + kPeriod};
}

inline TickDeadline TickDeadline::held() noexcept {
    return TickDeadline{std::nullopt};
}

inline std::optional<TickDeadline::Due> TickDeadline::take_due(MonoInstant now) noexcept {
    if (!next_)
        return std::nullopt;
    const auto step = grid_.advance({.next = *next_, .now = now});
    if (!step)
        return std::nullopt;
    next_ = step->next;
    return Due{step->latest, step->periods};
}

inline std::optional<timespec> TickDeadline::timeout(MonoInstant now) const noexcept {
    if (!next_)
        return std::nullopt;
    return os::to_timespec(grid_.remaining({.next = *next_, .now = now}));
}

}
