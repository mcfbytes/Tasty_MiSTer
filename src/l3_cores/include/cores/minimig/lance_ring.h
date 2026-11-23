// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/minimig/lance_addr.h"
#include "cores/minimig/lance_span.h"
#include "cores/minimig/lance_word.h"
#include "infra/seat.h"

namespace mister::cores::minimig {

struct LanceRingSlot {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr LanceRingSlot next() const noexcept { return LanceRingSlot{v + 1}; }

    [[nodiscard]] constexpr LanceSpan offset(LanceSpan stride) const noexcept { return stride * v; }

    friend constexpr bool operator==(LanceRingSlot, LanceRingSlot) = default;
};

struct LanceRingLength {
    std::uint32_t v = 0;

    static const LanceRingLength kMax;

    [[nodiscard]] static constexpr LanceRingLength of_negated(LanceWord w) noexcept {
        return LanceRingLength{static_cast<std::uint16_t>(-w.v)};
    }

    [[nodiscard]] static constexpr LanceRingLength of_ring_field(LanceAddr field) noexcept {
        constexpr unsigned kLengthShift = 29;
        constexpr std::uint32_t kLengthMask = 0x7;
        return LanceRingLength{1u << ((field.v >> kLengthShift) & kLengthMask)};
    }

    [[nodiscard]] constexpr bool accepted() const noexcept { return v != 0 && v <= kMax.v; }

    [[nodiscard]] constexpr LanceRingSlot wrap(LanceRingSlot s) const noexcept {
        return LanceRingSlot{s.v % v};
    }

    [[nodiscard]] constexpr LanceRingSlot last() const noexcept { return LanceRingSlot{v - 1}; }

    friend constexpr bool operator==(LanceRingLength, LanceRingLength) = default;
};

inline constexpr LanceRingLength LanceRingLength::kMax{512};

struct LanceRing {
    TASTY_SEAT_RESIDENT(Nic);

    static constexpr LanceSpan kStride{8};

    LanceAddr base{};
    LanceRingLength length{};
    LanceRingSlot cursor{};

    [[nodiscard]] constexpr bool walkable() const noexcept {
        return length.accepted() && !(base + length.last().offset(kStride)).beyond_ram();
    }

    [[nodiscard]] constexpr bool unconfigured() const noexcept { return !length.accepted(); }

    [[nodiscard]] constexpr LanceAddr descriptor() const noexcept {
        return base + length.wrap(cursor).offset(kStride);
    }

    constexpr void wrap() noexcept { cursor = length.wrap(cursor); }

    constexpr void advance() noexcept { cursor = cursor.next(); }
};

}
