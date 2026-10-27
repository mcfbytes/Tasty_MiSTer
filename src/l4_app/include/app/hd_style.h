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

inline constexpr std::array<HdStyle, 7> kHdPresets{
    kZaparooDark,
    HdStyle{.primary = 0x120e26u, .accent = 0xffbc4du, .text = 0xf2eeffu, .warning = 0xd6453du},
    HdStyle{.primary = 0x100b00u, .accent = 0xffb000u, .text = 0xffcc66u, .warning = 0xd6453du},
    HdStyle{.primary = 0x041004u, .accent = 0xb4d420u, .text = 0x9bbc0fu, .warning = 0xd6453du},
    HdStyle{.primary = 0x0d0d0du, .accent = 0xf83800u, .text = 0xfcfcfcu, .warning = 0xd6453du},
    HdStyle{.primary = 0x0e1116u, .accent = 0x46d9a8u, .text = 0xeef1f5u, .warning = 0xd6453du},
    HdStyle{.primary = 0x181858u, .accent = 0xf8d848u, .text = 0xf8f8f8u, .warning = 0xd6453du},
};

}
