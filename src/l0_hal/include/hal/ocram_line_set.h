// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/ocram_types.h"

namespace mister::hal::ocram {

template <class Owner>
class RingReader;

class LineSet {
public:
    constexpr LineSet() noexcept = default;

    [[nodiscard]] constexpr bool ring(Ring r) const noexcept { return (v & ring_bit(r)) != 0u; }

    [[nodiscard]] constexpr LineSet with(Ring r) const noexcept { return LineSet{v | ring_bit(r)}; }

    [[nodiscard]] constexpr LineSet without(Ring r) const noexcept {
        return LineSet{v & ~ring_bit(r)};
    }

    friend constexpr bool operator==(LineSet, LineSet) = default;

private:
    template <class Owner>
    friend class RingReader;

    constexpr explicit LineSet(std::uint32_t word) noexcept : v(word) {}

    [[nodiscard]] static constexpr LineSet of(std::uint32_t word) noexcept { return LineSet{word}; }

    [[nodiscard]] constexpr std::uint32_t word() const noexcept { return v; }

    [[nodiscard]] static constexpr std::uint32_t ring_bit(Ring r) noexcept {
        return OcramCaps::ring_bit(r);
    }

    std::uint32_t v = 0;
};

}
