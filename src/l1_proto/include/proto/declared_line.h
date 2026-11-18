// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"
#include "os/types.h"
#include "proto/block_line.h"
#include "proto/irq_binding.h"

namespace mister::proto {

struct DeclaredLine {
    std::uint8_t klass = 0;
    std::uint8_t pool = 0;
    std::uint32_t cause_off = 0;

    [[nodiscard]] constexpr bool declared() const noexcept { return pool != 0; }

    [[nodiscard]] constexpr bool agrees(BlockLine owner) const noexcept {
        return !declared()
            || (pool == os::to_ordinal(owner.line) + kIrqPoolFirstLine
                && owner.cause == hal::LwOffset{cause_off});
    }

    [[nodiscard]] static constexpr DeclaredLine of(std::uint32_t w) noexcept {
        return {
            .klass = static_cast<std::uint8_t>(w >> 28),
            .pool = static_cast<std::uint8_t>((w >> 24) & 0xFu),
            .cause_off = w & 0xFFFFFFu
        };
    }
};

}
