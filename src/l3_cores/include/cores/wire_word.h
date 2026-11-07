// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "hal/types.h"
#include "infra/seat.h"
#include "cores/wire_chunk.h"

namespace mister::cores {

struct WireWordBytes {
    static constexpr std::uint8_t kNarrow = 1;
    static constexpr std::uint8_t kWide = 2;
    std::uint8_t v = kNarrow;

    [[nodiscard]] static constexpr WireWordBytes of(std::uint8_t raw) noexcept {
        return WireWordBytes{raw == kWide ? kWide : kNarrow};
    }

    [[nodiscard]] static constexpr WireWordBytes of(hal::Width width) noexcept {
        return WireWordBytes{width == hal::Width::Word ? kWide : kNarrow};
    }

    [[nodiscard]] constexpr bool is_wide() const noexcept { return v == kWide; }

    [[nodiscard]] constexpr std::size_t scale(std::size_t words) const noexcept {
        return words * v;
    }

    friend constexpr bool operator==(WireWordBytes, WireWordBytes) = default;
};

}
