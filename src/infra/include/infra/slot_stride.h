// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <span>

#include "infra/byte_offset.h"

namespace mister::xthread {

struct PoolSlot;

struct SlotStride {
    std::size_t v = 0;

    [[nodiscard]] constexpr std::size_t bytes() const noexcept { return v; }

    template <class T>
    [[nodiscard]] constexpr std::span<T> subspan_in(
        std::span<T> slab,
        PoolSlot slot
    ) const noexcept;

    [[nodiscard]] constexpr ByteOffset offset_of(PoolSlot slot) const noexcept;
    friend constexpr bool operator==(SlotStride, SlotStride) = default;
    friend constexpr auto operator<=>(SlotStride, SlotStride) = default;
};

}
