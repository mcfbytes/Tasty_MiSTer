// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "cores/types.h"
#include "infra/affine.h"
#include "infra/seat.h"

namespace mister::cores {

struct PollTicks {
    TASTY_SEAT_EXEMPT(component);
    std::int32_t v = 0;
    using affine_count = PollTicks;
    using TickLength = std::chrono::duration<float, std::milli>;
    using Span = std::chrono::duration<std::uint32_t, std::milli>;

    struct Rate {
        SectorCount per_tick{};
    };

    [[nodiscard]] static constexpr PollTicks of_travel(SectorCount distance, Rate rate) noexcept {
        return PollTicks{distance.v / rate.per_tick.v};
    }

    [[nodiscard]] static constexpr PollTicks spanning(Span span, TickLength tick) noexcept {
        return PollTicks{
            static_cast<std::int32_t>(static_cast<float>(span.count()) / tick.count())
        };
    }

    [[nodiscard]] constexpr bool owed() const noexcept { return v > 0; }

    constexpr PollTicks& spend() noexcept {
        if (v > 0)
            --v;
        return *this;
    }

    friend constexpr bool operator==(PollTicks, PollTicks) = default;
    friend constexpr auto operator<=>(PollTicks, PollTicks) = default;
};

static_assert(
    PollTicks::spanning(PollTicks::Span{220}, PollTicks::TickLength{13.33F}) == PollTicks{16}
);

static_assert(
    PollTicks::of_travel(SectorCount{-4001}, PollTicks::Rate{SectorCount{2000}}) == PollTicks{-2}
);

}
