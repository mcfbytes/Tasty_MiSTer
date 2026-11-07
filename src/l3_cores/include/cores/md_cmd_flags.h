// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores::md {

struct CmdFlags {

    enum Bit : std::uint8_t {
        Play = 0x01,
        Stop = 0x02,
        Resume = 0x04,
        Volume = 0x08,
        Loop = 0x10,
    };

    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool has(Bit bit) const noexcept { return (v & bit) != 0u; }

    [[nodiscard]] constexpr bool none() const noexcept { return v == 0u; }

    friend constexpr bool operator==(CmdFlags, CmdFlags) = default;
};

}
