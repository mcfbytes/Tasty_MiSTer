// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace mister::app {

struct HdStyle {
    std::uint32_t primary = 0;
    std::uint32_t accent = 0;
    std::uint32_t text = 0;
    std::uint32_t warning = 0;
};

inline constexpr HdStyle kZaparooDark{
    .primary = 0x050608u,
    .accent = 0x168bffu,
    .text = 0xf7f7f5u,
    .warning = 0xd6453du,
};

inline constexpr std::array<HdStyle, 1> kHdPresets{kZaparooDark};

}  // namespace mister::app
