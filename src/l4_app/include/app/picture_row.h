// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

#include "infra/affine.h"
#include "infra/decimal_text.h"

namespace mister::app {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator+=;

struct PictureRow;

struct PictureRows {
    std::uint16_t v = 0;
    using affine_count = PictureRows;
    static constexpr std::uint16_t kHalfDivisor = 2;

    [[nodiscard]] constexpr PictureRow sample_row(std::uint32_t y, PictureRows out) const noexcept;

    [[nodiscard]] constexpr bool fits_in(std::size_t rows) const noexcept { return v <= rows; }

    [[nodiscard]] constexpr PictureRows halved() const noexcept {
        return PictureRows{static_cast<std::uint16_t>(v / kHalfDivisor)};
    }

    [[nodiscard]] constexpr std::uint16_t halved_if(bool half) const noexcept {
        return static_cast<std::uint16_t>(half ? v / kHalfDivisor : v);
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

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

template <class F>
constexpr void for_each_row(PictureRows rows, F&& f) {
    for (PictureRow y{}; y < PictureRow{} + rows; y += PictureRows{1})
        f(y);
}

}
