// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/spi_halves.h"

namespace mister::proto {

struct AuxValue {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr hal::SpiHalves halves() const noexcept { return hal::SpiHalves::of(v); }

    [[nodiscard]] static constexpr AuxValue of(hal::SpiHalves h) noexcept {
        return h.joined([](std::uint32_t d) { return AuxValue{d}; });
    }

    friend constexpr bool operator==(AuxValue, AuxValue) = default;
};

}
