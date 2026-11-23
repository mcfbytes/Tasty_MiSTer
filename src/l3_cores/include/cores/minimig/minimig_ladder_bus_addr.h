// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/minimig/minimig_ladder_bus_bytes.h"
#include "infra/affine.h"
#include "infra/endian.h"

namespace mister::cores::minimig {

struct BusAddr {
    std::uint32_t v = 0;
    using affine_count = BusBytes;

    [[nodiscard]] constexpr std::array<std::byte, sizeof(std::uint32_t)>
    wire_bytes() const noexcept {
        std::array<std::byte, sizeof(std::uint32_t)> out{};
        infra::store_le<std::uint32_t>(std::span<std::byte, sizeof(std::uint32_t)>(out), v);
        return out;
    }

    [[nodiscard]] constexpr std::array<std::byte, sizeof(std::uint32_t)>
    long_bytes() const noexcept {
        std::array<std::byte, sizeof(std::uint32_t)> out{};
        infra::store_be<std::uint32_t>(std::span<std::byte, sizeof(std::uint32_t)>(out), v);
        return out;
    }

    friend constexpr bool operator==(BusAddr, BusAddr) = default;
    friend constexpr auto operator<=>(BusAddr, BusAddr) = default;
};

inline constexpr BusAddr kKickstartRom{0xF80000};
inline constexpr BusAddr kKickstartMirror{0xFC0000};
inline constexpr BusAddr kExtendedRom{0xE00000};
inline constexpr BusAddr kHrtmonRom{0xA10000};
inline constexpr BusAddr kCustomChips{0xDFF000};
inline constexpr BusAddr kBootScreen{0x080000};

static_assert(
    kKickstartRom.wire_bytes()[0] == std::byte{0x00}
        && kKickstartRom.wire_bytes()[2] == std::byte{0xF8},
    "minimig_config.cpp:82-86: the address goes out low byte first"
);
static_assert(kKickstartRom + (kKickstartMirror - kKickstartRom) == kKickstartMirror);

}
