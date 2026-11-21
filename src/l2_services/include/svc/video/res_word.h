// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>

#include "hal/types.h"
#include "infra/decimal_text.h"

namespace mister::svc::video {

struct ResWord {
    std::uint16_t v = 0;
    static constexpr std::uint16_t kInterlacedBit = 0x100;
    static constexpr std::uint16_t kRotatedBit = 0x200;

    [[nodiscard]] static constexpr ResWord of(hal::SpiWord reply) noexcept {
        return ResWord{reply.decoded(std::identity{})};
    }

    [[nodiscard]] constexpr bool interlaced() const noexcept { return (v & kInterlacedBit) != 0; }

    [[nodiscard]] constexpr bool rotated() const noexcept { return (v & kRotatedBit) != 0; }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(ResWord, ResWord) = default;
};

}
