// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <compare>
#include <cstdint>

#include "reactor/tick.h"
#include "reactor/tick_deadline.h"

namespace mister::reactor {

struct TickNo {
    std::uint64_t v = 0;
    using affine_count = TickDeadline::Periods;

    [[nodiscard]] constexpr TickNo after(std::chrono::milliseconds period) const noexcept {
        return TickNo{v + static_cast<std::uint64_t>(period.count())};
    }

    [[nodiscard]] constexpr std::chrono::milliseconds elapsed() const noexcept {
        return std::chrono::milliseconds{static_cast<std::int64_t>(v)};
    }

    friend constexpr bool operator==(TickNo, TickNo) = default;
    friend constexpr auto operator<=>(TickNo, TickNo) = default;
};

static_assert(kTickNs == 1'000'000, "elapsed() reads one tick as one millisecond");

inline constexpr TickNo kNeverTick{~std::uint64_t{0}};

}
