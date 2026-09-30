// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace mister::hal {

struct ScalerHeader {
    static constexpr std::size_t kBytes = 16;
    static constexpr std::uint8_t kTypeImage = 1;
    static constexpr std::uint8_t kFormatRgb24 = 1;

    std::uint8_t type = 0;
    std::uint8_t format = 0;
    std::uint16_t header_size = 0;
    std::uint16_t attributes = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t line = 0;
    std::uint16_t output_width = 0;
    std::uint16_t output_height = 0;

    [[nodiscard]] static constexpr ScalerHeader decode(
        std::span<const std::byte, kBytes> raw) noexcept {
        auto b = [raw](std::size_t i) constexpr noexcept {
            return static_cast<std::uint8_t>(raw[i]);
        };
        auto be16 = [b](std::size_t i) constexpr noexcept {
            return static_cast<std::uint16_t>(static_cast<std::uint16_t>(b(i) << 8) | b(i + 1));
        };
        return ScalerHeader{.type = b(0),
                            .format = b(1),
                            .header_size = be16(2),
                            .attributes = be16(4),
                            .width = be16(6),
                            .height = be16(8),
                            .line = be16(10),
                            .output_width = be16(12),
                            .output_height = be16(14)};
    }

    [[nodiscard]] constexpr bool supported() const noexcept {
        return type == kTypeImage && format == kFormatRgb24;
    }

    [[nodiscard]] constexpr std::uint8_t frame_counter() const noexcept {
        return static_cast<std::uint8_t>((attributes >> 5) & 0x7u);
    }
    [[nodiscard]] constexpr bool triple_buffered() const noexcept {
        return (attributes & 0x0010u) != 0;
    }
    [[nodiscard]] constexpr bool interlaced() const noexcept { return (attributes & 0x0001u) != 0; }
};

}  // namespace mister::hal
