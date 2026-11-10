// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/byte_offset.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "infra/slot_stride.h"

namespace mister::xthread {

template <class Slot, std::size_t N, SeatTag Owner>
class SlotPool;

struct PoolSize {
    std::size_t v = 0;
    friend constexpr bool operator==(PoolSize, PoolSize) = default;
    friend constexpr auto operator<=>(PoolSize, PoolSize) = default;
};

struct PoolSlot {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool within(PoolSize count) const noexcept {
        return static_cast<std::size_t>(v) < count.v;
    }

    [[nodiscard]] constexpr ByteOffset offset(SlotStride stride) const noexcept {
        return stride.offset_of(*this);
    }

    template <std::integral Word>
    [[nodiscard]] constexpr Word byte_start(Word stride) const noexcept {
        return static_cast<Word>(static_cast<std::size_t>(v) * static_cast<std::size_t>(stride));
    }

    template <class T>
    [[nodiscard]] constexpr std::span<T> subspan_of(
        std::span<T> slab,
        SlotStride stride
    ) const noexcept {
        return stride.subspan_in(slab, *this);
    }

    template <class T, std::size_t M>
    [[nodiscard]] constexpr T& pick(T (&table)[M]) const noexcept {
        return table[v];
    }

    friend constexpr bool operator==(PoolSlot, PoolSlot) = default;
    friend constexpr auto operator<=>(PoolSlot, PoolSlot) = default;

private:
    template <class, std::size_t, SeatTag>
    friend class SlotPool;

    friend struct SlotPoolTestAccess;

    template <std::integral I>
    [[nodiscard]] static constexpr PoolSlot of(I i) noexcept {
        return PoolSlot{static_cast<std::uint8_t>(i)};
    }
};

constexpr ByteOffset SlotStride::offset_of(PoolSlot slot) const noexcept {
    return ByteOffset{slot.byte_start(v)};
}

template <infra::ArrayIndex Index>
[[nodiscard]] constexpr Index index_as(PoolSlot slot) noexcept {
    return Index{slot.v};
}

template <class T>
constexpr std::span<T> SlotStride::subspan_in(std::span<T> slab, PoolSlot slot) const noexcept {
    return slab.subspan(slot.byte_start<std::size_t>(v), v);
}

}
