// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "infra/endian.h"

namespace mister::cores::ide {

struct AtapiVolume {
    std::uint8_t v = std::numeric_limits<std::uint8_t>::max();

    [[nodiscard]] static constexpr AtapiVolume of(std::byte level) noexcept {
        return AtapiVolume{std::to_integer<std::uint8_t>(level)};
    }

    [[nodiscard]] constexpr std::byte level_byte() const noexcept { return std::byte{v}; }

    constexpr void apply(std::span<std::byte, sizeof(std::int16_t)> sample) const noexcept {
        constexpr std::int32_t kSteps = std::numeric_limits<std::uint8_t>::max() + 1;
        const std::int32_t in = infra::load_le<std::int16_t>(sample);
        infra::store_le<std::int16_t>(
            sample,
            static_cast<std::int16_t>((in * (std::int32_t{v} + 1)) / kSteps)
        );
    }

    friend constexpr bool operator==(AtapiVolume, AtapiVolume) = default;
};

static_assert(sizeof(AtapiVolume) == 1, "the level is the page's one byte");

}
