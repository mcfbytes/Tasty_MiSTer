// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "hal/spi_halves.h"

namespace mister::proto {

struct AuxValue {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr hal::SpiHalves halves() const noexcept { return hal::SpiHalves::of(v); }

    friend constexpr bool operator==(AuxValue, AuxValue) = default;
};

struct SessionParams {
    AuxValue aux{};
    std::string_view ext{};
};

}
