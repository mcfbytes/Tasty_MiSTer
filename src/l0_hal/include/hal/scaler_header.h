// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "hal/types.h"
#include "infra/seat.h"

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
        std::span<const std::byte, kBytes> raw
    ) noexcept {
        auto b = [raw](std::size_t i) constexpr noexcept {
            return static_cast<std::uint8_t>(raw[i]);
        };
        constexpr unsigned kByteBits = 8;
        auto be16 = [b](std::size_t i) constexpr noexcept {
            return static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(b(i) << kByteBits) | b(i + 1)
            );
        };

        constexpr std::size_t kAtType = 0, kAtFormat = 1, kAtHeaderSize = 2, kAtAttributes = 4,
                              kAtWidth = 6, kAtHeight = 8, kAtLine = 10, kAtOutputWidth = 12,
                              kAtOutputHeight = 14;
        return ScalerHeader{
            .type = b(kAtType),
            .format = b(kAtFormat),
            .header_size = be16(kAtHeaderSize),
            .attributes = be16(kAtAttributes),
            .width = be16(kAtWidth),
            .height = be16(kAtHeight),
            .line = be16(kAtLine),
            .output_width = be16(kAtOutputWidth),
            .output_height = be16(kAtOutputHeight)
        };
    }

    [[nodiscard]] constexpr bool supported() const noexcept {
        return type == kTypeImage && format == kFormatRgb24;
    }

    [[nodiscard]] constexpr HeaderCtr frame_counter() const noexcept {
        constexpr unsigned kFrameCounterShift = 5;
        return HeaderCtr{
            static_cast<std::uint8_t>((attributes >> kFrameCounterShift) & HeaderCtr::kMask)
        };
    }

    [[nodiscard]] constexpr bool triple_buffered() const noexcept {
        constexpr std::uint16_t kAttrTriple = 0x0010;
        return (attributes & kAttrTriple) != 0;
    }

    [[nodiscard]] constexpr bool interlaced() const noexcept {
        constexpr std::uint16_t kAttrInterlaced = 0x0001;
        return (attributes & kAttrInterlaced) != 0;
    }

    [[nodiscard]] constexpr bool operator==(const ScalerHeader&) const noexcept = default;
};

}
