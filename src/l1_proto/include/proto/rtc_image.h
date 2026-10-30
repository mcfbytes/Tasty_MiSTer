// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "infra/error.h"

namespace mister::proto {

struct RtcImage {
    std::array<std::uint8_t, 8> rtc{};
    std::uint32_t stamp = 0;
    [[nodiscard]] constexpr bool operator==(const RtcImage&) const = default;
};

static_assert(sizeof(RtcImage) == 12 && alignof(RtcImage) == 4);
static_assert(std::is_trivially_copyable_v<RtcImage>);
static_assert(std::has_unique_object_representations_v<RtcImage>);

[[nodiscard]] Ex<RtcImage> rtc_image_at(std::uint32_t unix_seconds) noexcept;

}
