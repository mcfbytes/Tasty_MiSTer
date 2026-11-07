// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <type_traits>

#include "infra/error.h"
#include "proto/types.h"

namespace mister::proto {

enum class RtcField : std::size_t {
    Sec = 0,
    Min,
    Hour,
    Mday,
    Mon,
    Year,
    Wday,
    Control,
};

inline constexpr std::uint8_t kRtcControl = 0x40;

struct RtcImage {
    std::array<std::uint8_t, 8> rtc{};
    std::uint32_t stamp = 0;

    [[nodiscard]] constexpr std::uint8_t& byte(RtcField field) noexcept {
        return rtc[static_cast<std::size_t>(field)];
    }

    [[nodiscard]] constexpr std::uint8_t byte(RtcField field) const noexcept {
        return rtc[static_cast<std::size_t>(field)];
    }

    constexpr void put(RtcField field, PackedBcd value) noexcept { byte(field) = value.v; }

    constexpr void put_binary(RtcField field, std::uint8_t value) noexcept { byte(field) = value; }

    [[nodiscard]] static constexpr RtcImage zero_date() noexcept {
        RtcImage image{};
        image.put_binary(RtcField::Control, kRtcControl);
        return image;
    }

    [[nodiscard]] constexpr bool operator==(const RtcImage&) const = default;
};

static_assert(sizeof(RtcImage) == 12 && alignof(RtcImage) == 4);
static_assert(std::is_trivially_copyable_v<RtcImage>);
static_assert(std::has_unique_object_representations_v<RtcImage>);

[[nodiscard]] Ex<RtcImage> rtc_image_at(std::time_t unix_seconds) noexcept;

}
