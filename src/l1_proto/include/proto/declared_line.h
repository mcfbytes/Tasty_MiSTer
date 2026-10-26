// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::proto {

struct DeclaredLine {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint8_t klass = 0;
    std::uint8_t pool = 0;
    std::uint32_t cause_off = 0;

    [[nodiscard]] constexpr bool declared() const noexcept { return pool != 0; }

    [[nodiscard]] static constexpr DeclaredLine of(std::uint32_t w) noexcept {
        return {
            .klass = static_cast<std::uint8_t>(w >> 28),
            .pool = static_cast<std::uint8_t>((w >> 24) & 0xFu),
            .cause_off = w & 0xFFFFFFu
        };
    }
};

}
