// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "svc/modeline.h"

namespace mister::svc {

enum class ModelineItem : std::size_t {
    Mode = 0,
    Hact = 1,
    Hfp = 2,
    Hs = 3,
    Hbp = 4,
    Vact = 5,
    Vfp = 6,
    Vs = 7,
    Vbp = 8,
    PllFirst = 9,
    PllLast = 20,
    Hpol = 21,
    Vpol = 22,
    Vic = 23,
    Rb = 24,
    PixelRepeat = 25,
    Count = 26,
};

inline constexpr std::size_t kModelineItems = static_cast<std::size_t>(ModelineItem::Count);
inline constexpr std::size_t kModelineTimings = 8;
inline constexpr std::size_t kModelinePllWords = 12;

[[nodiscard]] constexpr std::size_t item(ModelineItem i) noexcept {
    return static_cast<std::size_t>(i);
}

static_assert(
    item(ModelineItem::PllLast) - item(ModelineItem::PllFirst) + 1 == kModelinePllWords,
    "item[9..20] holds the twelve PLL words"
);
static_assert(
    item(ModelineItem::Vbp) - item(ModelineItem::Hact) + 1 == kModelineTimings,
    "item[1..8] holds the eight timings"
);
static_assert(
    item(ModelineItem::PixelRepeat) + 1 == kModelineItems,
    "PixelRepeat is the last flag slot"
);

}
