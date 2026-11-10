// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/affine.h"
#include "infra/decimal_text.h"
#include "infra/endian.h"

namespace mister::app {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator+=;

struct PictureRow;

struct PictureRows {
    std::uint16_t v = 0;
    using affine_count = PictureRows;
    static constexpr std::uint16_t kHalfDivisor = 2;

    [[nodiscard]] constexpr PictureRow sample_row(std::uint32_t y, PictureRows out) const noexcept;

    [[nodiscard]] constexpr PictureRow sample_row(PictureRows at, PictureRows out) const noexcept;

    [[nodiscard]] constexpr bool fits_in(std::size_t rows) const noexcept { return v <= rows; }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr PictureRows halved() const noexcept {
        return PictureRows{static_cast<std::uint16_t>(v / kHalfDivisor)};
    }

    template <std::integral I>
    [[nodiscard]] constexpr PictureRows divided_by(I divisor) const noexcept {
        return PictureRows{static_cast<std::uint16_t>(v / divisor)};
    }

    template <std::integral I>
    [[nodiscard]] static constexpr PictureRows of(I n) noexcept {
        return PictureRows{static_cast<std::uint16_t>(n)};
    }

    template <std::integral T>
    [[nodiscard]] constexpr T count() const noexcept {
        return static_cast<T>(v);
    }

private:
    friend class HpsFramebuffer;

    [[nodiscard]] constexpr int clamp_side(int requested, int floor) const noexcept {
        return requested < floor || requested > static_cast<int>(v) ? static_cast<int>(v)
                                                                    : requested;
    }

public:
    [[nodiscard]] constexpr int last_index() const noexcept { return static_cast<int>(v) - 1; }

    [[nodiscard]] constexpr std::uint16_t wire_word() const noexcept {
        return count<std::uint16_t>();
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    constexpr void store_le16(std::span<std::byte, sizeof(std::uint16_t)> dst) const noexcept {
        infra::store_le<std::uint16_t>(dst, v);
    }

    constexpr void store_le32(std::span<std::byte, sizeof(std::uint32_t)> dst) const noexcept {
        infra::store_le<std::uint32_t>(dst, v);
    }

    constexpr void store_be32(std::span<std::uint8_t, sizeof(std::uint32_t)> dst) const noexcept {
        infra::store_be<std::uint32_t>(dst, v);
    }

    [[nodiscard]] constexpr bool operator==(const PictureRows&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PictureRows&) const noexcept = default;
};

struct PictureRow {
    std::uint16_t v = 0;
    using affine_count = PictureRows;

    [[nodiscard]] constexpr PictureRow doubled() const noexcept {
        return PictureRow{static_cast<std::uint16_t>(v * PictureRows::kHalfDivisor)};
    }

    [[nodiscard]] constexpr PictureRow flipped_in(PictureRows height) const noexcept {
        return PictureRow{static_cast<std::uint16_t>(height.v - 1u - v)};
    }

    [[nodiscard]] constexpr bool operator==(const PictureRow&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PictureRow&) const noexcept = default;
};

constexpr PictureRow PictureRows::sample_row(std::uint32_t y, PictureRows out) const noexcept {
    const std::uint32_t at = y * v / out.v;
    return PictureRow{static_cast<std::uint16_t>(at >= v ? v - 1u : at)};
}

constexpr PictureRow PictureRows::sample_row(PictureRows y, PictureRows out) const noexcept {
    const std::uint32_t at = static_cast<std::uint32_t>(y.v) * v / out.v;
    return PictureRow{static_cast<std::uint16_t>(at >= v ? v - 1u : at)};
}

template <class F>
constexpr void for_each_row(PictureRows rows, F&& f) {
    for (PictureRow y{}; y < PictureRow{} + rows; y += PictureRows{1})
        f(y);
}

}
