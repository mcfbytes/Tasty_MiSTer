// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <optional>

#include "infra/seat.h"
#include "reactor/tick.h"
#include "reactor/types.h"

namespace mister::reactor {

class TickDeadline {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::chrono::nanoseconds kPeriod{kTickNs};

    struct Periods {
        std::uint64_t v;
        using affine_count = Periods;

        [[nodiscard]] constexpr std::uint64_t missed() const noexcept { return v - 1; }

        [[nodiscard]] constexpr std::uint64_t ticks() const noexcept { return v; }
    };

    struct Due {
        MonoInstant latest;
        Periods periods;
    };

    [[nodiscard]] static TickDeadline after(MonoInstant now) noexcept;
    [[nodiscard]] static TickDeadline held() noexcept;

    [[nodiscard]] std::optional<Due> take_due(MonoInstant now) noexcept;

    [[nodiscard]] std::optional<timespec> timeout(MonoInstant now) const noexcept;

private:
    TickDeadline(MonoInstant first, bool held) noexcept : next_(first), held_(held) {}

    static constexpr std::int64_t kNanosPerSecond = 1'000'000'000;

    MonoInstant next_;
    bool held_;
};

inline TickDeadline TickDeadline::after(MonoInstant now) noexcept {
    return TickDeadline{now + kPeriod, false};
}

inline TickDeadline TickDeadline::held() noexcept {
    return TickDeadline{MonoInstant{}, true};
}

inline std::optional<TickDeadline::Due> TickDeadline::take_due(MonoInstant now) noexcept {
    if (held_ || now < next_)
        return std::nullopt;

    if (now < next_ + kPeriod) {
        const Due due{next_, Periods{1}};
        next_ += kPeriod;
        return due;
    }

    const std::int64_t late = std::chrono::duration_cast<std::chrono::nanoseconds>(now - next_)
                                  .count();
    const auto n = 1u + static_cast<std::uint64_t>(late / kPeriod.count());
    const MonoInstant latest = next_
        + std::chrono::nanoseconds{static_cast<std::int64_t>(n - 1u) * kPeriod.count()};
    next_ = latest + kPeriod;
    return Due{latest, Periods{n}};
}

inline std::optional<timespec> TickDeadline::timeout(MonoInstant now) const noexcept {
    if (held_)
        return std::nullopt;
    std::int64_t rel = std::chrono::duration_cast<std::chrono::nanoseconds>(next_ - now).count();
    if (rel < 0)
        rel = 0;

    timespec ts{};
    ts.tv_sec = static_cast<time_t>(rel / kNanosPerSecond);
    ts.tv_nsec = static_cast<long>(rel % kNanosPerSecond);
    return ts;
}

}
