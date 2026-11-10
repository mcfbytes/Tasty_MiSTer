// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "app/row_bytes.h"
#include "app/types.h"
#include "infra/affine.h"
#include "infra/decimal_text.h"
#include "infra/endian.h"

namespace mister::app {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator+=;

struct PicturePixels {
    std::uint16_t v = 0;
    using affine_count = PicturePixels;
    static constexpr std::uint16_t kHalfDivisor = 2;

    template <std::integral I>
    [[nodiscard]] static constexpr PicturePixels of(I n) noexcept {
        return PicturePixels{static_cast<std::uint16_t>(n)};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr PicturePixels halved() const noexcept {
        return PicturePixels{static_cast<std::uint16_t>(v / kHalfDivisor)};
    }

    template <std::integral I>
    [[nodiscard]] constexpr PicturePixels divided_by(I divisor) const noexcept {
        return PicturePixels{static_cast<std::uint16_t>(v / divisor)};
    }

    [[nodiscard]] constexpr RowBytes row_bytes(PixelBytes pixel_bytes) const noexcept {
        return RowBytes{pixel_bytes.span(v)};
    }

    [[nodiscard]] constexpr RowBytes row_bytes(
        PixelBytes pixel_bytes,
        RowAlign align
    ) const noexcept {
        return RowBytes{align.round_up(pixel_bytes.span(v))};
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
    [[nodiscard]] constexpr PixelIndex last_index() const noexcept {
        return PixelIndex{static_cast<std::uint32_t>(v) - 1u};
    }

    [[nodiscard]] constexpr PicturePixels sample_column(
        PicturePixels at,
        PicturePixels out
    ) const noexcept {
        const std::uint32_t x = static_cast<std::uint32_t>(at.v) * v / out.v;
        return PicturePixels{static_cast<std::uint16_t>(x >= v ? v - 1u : x)};
    }

    [[nodiscard]] constexpr std::span<std::byte> pixel_at(
        std::span<std::byte> line,
        PixelBytes pixel_bytes
    ) const noexcept {
        return line.subspan(pixel_bytes.span(v));
    }

    [[nodiscard]] constexpr std::span<const std::byte> pixel_at(
        std::span<const std::byte> line,
        PixelBytes pixel_bytes
    ) const noexcept {
        return line.subspan(pixel_bytes.span(v));
    }

    [[nodiscard]] constexpr std::uint16_t wire_word() const noexcept {
        return count<std::uint16_t>();
    }

    constexpr void store_le16(std::span<std::byte, sizeof(std::uint16_t)> dst) const noexcept {
        infra::store_le<std::uint16_t>(dst, v);
    }

    constexpr void store_le32(std::span<std::byte, sizeof(std::uint32_t)> dst) const noexcept {
        infra::store_le<std::uint32_t>(dst, v);
    }

    constexpr void store_be32(std::span<std::uint8_t, sizeof(std::uint32_t)> dst) const noexcept {
        infra::store_be<std::uint32_t>(dst, v);
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(PicturePixels, PicturePixels) = default;
    friend constexpr auto operator<=>(PicturePixels, PicturePixels) = default;
};

}
