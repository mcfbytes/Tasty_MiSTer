// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/endian.h"

namespace mister::cores::x86 {

struct DosAttr {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr DosAttr of(std::byte raw) noexcept {
        return DosAttr{std::to_integer<std::uint8_t>(raw)};
    }

    [[nodiscard]] constexpr bool has(DosAttr flag) const noexcept { return (v & flag.v) != 0; }

    constexpr void store(std::span<std::byte, 1> out) const noexcept { out[0] = std::byte{v}; }

    friend constexpr bool operator==(DosAttr, DosAttr) = default;
};

inline constexpr DosAttr kDosAttrReadOnly{0x01};
inline constexpr DosAttr kDosAttrHidden{0x02};
inline constexpr DosAttr kDosAttrSystem{0x04};
inline constexpr DosAttr kDosAttrVolume{0x08};
inline constexpr DosAttr kDosAttrDirectory{0x10};
inline constexpr DosAttr kDosAttrArchive{0x20};
inline constexpr DosAttr kDosAttrDevice{0x40};

}
