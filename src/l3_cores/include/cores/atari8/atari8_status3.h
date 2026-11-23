// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "cores/types.h"
#include "hal/types.h"

namespace mister::cores::atari8 {

struct Atari8Status3 {
    hal::SpiWord v{};

    [[nodiscard]] constexpr ClockHz core_hz() const noexcept {
        const hal::SpiWord code = v.masked(kCoreClock);
        for (const Clock& c : kClocks)
            if (c.code == code)
                return c.hz;
        return ClockHz{};
    }

    friend constexpr bool operator==(Atari8Status3, Atari8Status3) = default;

    static constexpr hal::SpiWord kCoreClock{0x0007};

private:
    struct Clock {
        hal::SpiWord code;
        ClockHz hz;
    };

    static constexpr auto kClocks = std::to_array<Clock>({
        {hal::SpiWord{0b100}, ClockHz{28375152}},
        {hal::SpiWord{0b101}, ClockHz{28454400}},
        {hal::SpiWord{0b000}, ClockHz{28636364}},
        {hal::SpiWord{0b001}, ClockHz{28644608}},
        {hal::SpiWord{0b010}, ClockHz{28673280}},
    });
};

}
