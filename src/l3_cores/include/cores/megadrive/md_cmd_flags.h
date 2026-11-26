// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"

namespace mister::cores::megadrive::md {

struct CmdFlags {

    enum Bit : std::uint8_t {
        Play = 0x01,
        Stop = 0x02,
        Resume = 0x04,
        Volume = 0x08,
        Loop = 0x10,
    };

    std::uint8_t v = 0;

    [[nodiscard]] static constexpr CmdFlags of_word(hal::LinkWord w) noexcept {
        return CmdFlags{w.low_byte()};
    }

    [[nodiscard]] constexpr bool has(Bit bit) const noexcept { return (v & bit) != 0u; }

    [[nodiscard]] constexpr bool none() const noexcept { return v == 0u; }

    friend constexpr bool operator==(CmdFlags, CmdFlags) = default;
};

static_assert(
    CmdFlags::of_word(hal::LinkWord{0x1203}) == CmdFlags{CmdFlags::Play | CmdFlags::Stop},
    "the flags ride the low byte; the high byte is the track"
);

}
