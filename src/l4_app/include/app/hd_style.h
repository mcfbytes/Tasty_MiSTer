// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "app/types.h"

namespace mister::app {

struct HdStyle {
    Rgb888 primary{};
    Rgb888 accent{};
    Rgb888 text{};
    Rgb888 warning{};
};

inline constexpr Rgb888 kHdPresetWarning{0xd6453du};

inline constexpr HdStyle kZaparooDark{
    .primary = Rgb888{0x050608u},
    .accent = Rgb888{0x168bffu},
    .text = Rgb888{0xf7f7f5u},
    .warning = kHdPresetWarning,
};

inline constexpr std::array<HdStyle, 7> kHdPresets{
    kZaparooDark,
    HdStyle{
        .primary = Rgb888{0x120e26u},
        .accent = Rgb888{0xffbc4du},
        .text = Rgb888{0xf2eeffu},
        .warning = kHdPresetWarning
    },
    HdStyle{
        .primary = Rgb888{0x100b00u},
        .accent = Rgb888{0xffb000u},
        .text = Rgb888{0xffcc66u},
        .warning = kHdPresetWarning
    },
    HdStyle{
        .primary = Rgb888{0x041004u},
        .accent = Rgb888{0xb4d420u},
        .text = Rgb888{0x9bbc0fu},
        .warning = kHdPresetWarning
    },
    HdStyle{
        .primary = Rgb888{0x0d0d0du},
        .accent = Rgb888{0xf83800u},
        .text = Rgb888{0xfcfcfcu},
        .warning = kHdPresetWarning
    },
    HdStyle{
        .primary = Rgb888{0x0e1116u},
        .accent = Rgb888{0x46d9a8u},
        .text = Rgb888{0xeef1f5u},
        .warning = kHdPresetWarning
    },
    HdStyle{
        .primary = Rgb888{0x181858u},
        .accent = Rgb888{0xf8d848u},
        .text = Rgb888{0xf8f8f8u},
        .warning = kHdPresetWarning
    },
};

}
