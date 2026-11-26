// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>

#include "hal/types.h"
#include "infra/error.h"

namespace mister::proto {

struct SaveRequestWord {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr SaveRequestWord of(hal::LinkWord reply) noexcept {
        return SaveRequestWord{reply.decoded(std::identity{})};
    }

    [[nodiscard]] constexpr bool requested() const noexcept { return v != 0; }

    [[nodiscard]] constexpr std::uint8_t slot() const noexcept {
        return static_cast<std::uint8_t>(v >> 8);
    }

    friend constexpr bool operator==(SaveRequestWord, SaveRequestWord) = default;
};

}
