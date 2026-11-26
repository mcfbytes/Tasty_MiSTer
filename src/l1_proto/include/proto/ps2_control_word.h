// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/types.h"

namespace mister::proto {

struct Ps2ControlWord {
    hal::LinkWord word{};

    static constexpr hal::LinkWord kBytePresent = hal::LinkWord{}.with_high_byte(1);

    [[nodiscard]] constexpr std::optional<std::uint8_t> byte() const noexcept {
        if (!word.intersects(kBytePresent))
            return std::nullopt;
        return word.low_byte();
    }

    [[nodiscard]] static constexpr Ps2ControlWord carrying(std::uint8_t b) noexcept {
        return Ps2ControlWord{kBytePresent.with(hal::LinkWord{b})};
    }

    friend constexpr bool operator==(Ps2ControlWord, Ps2ControlWord) = default;
};

static_assert(sizeof(Ps2ControlWord) == 2);
static_assert(Ps2ControlWord::kBytePresent == hal::LinkWord{0x0100});
static_assert(
    !Ps2ControlWord{}.byte() && Ps2ControlWord::carrying(0xF0).byte() == std::uint8_t{0xF0}
);
static_assert(Ps2ControlWord::carrying(0xF0).word == hal::LinkWord{0x01F0});

}
