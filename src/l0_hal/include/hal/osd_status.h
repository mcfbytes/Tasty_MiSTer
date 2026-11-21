// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/reg_word.h"

namespace mister::hal {

struct OsdDrops {
    std::uint32_t v = 0;
    friend constexpr bool operator==(OsdDrops, OsdDrops) = default;
};

struct OsdStatus {
    std::uint32_t v = 0;

    enum Bits : std::uint32_t { kLevelMask = 0x01FF0000u, kDropsMask = 0xFFFFu };

    [[nodiscard]] constexpr bool drained() const noexcept { return (v & kLevelMask) == 0; }

    [[nodiscard]] constexpr OsdDrops drops() const noexcept { return OsdDrops{v & kDropsMask}; }

    [[nodiscard]] static constexpr OsdStatus of(RegWord w) noexcept { return OsdStatus{w.bits()}; }

    friend constexpr bool operator==(OsdStatus, OsdStatus) = default;
};

}
