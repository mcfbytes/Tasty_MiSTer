// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/reg_word.h"

namespace mister::hal {

struct EnableWord {

    enum Bits : std::uint32_t {
        kEnable = 1u << 31,
        kQuiet = 1u << 30,
        kStale = 1u << 29,
        kBurst = 1u << 28,
        kHardReset = 1u << 15,
        kIdView = 1u << 11,
    };

    static constexpr RegWord kKey{0x4C570001u};

    std::uint32_t v = 0;

    [[nodiscard]] static constexpr EnableWord of(RegWord w) noexcept {
        return EnableWord{w.bits()};
    }

    [[nodiscard]] constexpr RegWord register_word() const noexcept { return RegWord{v}; }

    [[nodiscard]] constexpr bool enabled() const noexcept { return (v & kEnable) != 0u; }

    [[nodiscard]] constexpr bool quiet() const noexcept { return (v & kQuiet) != 0u; }

    [[nodiscard]] constexpr bool stale() const noexcept { return (v & kStale) != 0u; }

    [[nodiscard]] constexpr bool burst() const noexcept { return (v & kBurst) != 0u; }

    [[nodiscard]] constexpr bool hard_reset() const noexcept { return (v & kHardReset) != 0u; }

    [[nodiscard]] constexpr bool id_view() const noexcept { return (v & kIdView) != 0u; }

    [[nodiscard]] static constexpr RegWord key() noexcept { return kKey; }

    friend constexpr bool operator==(EnableWord, EnableWord) = default;
};

}
