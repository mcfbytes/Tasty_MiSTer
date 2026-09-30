// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/phys_region.h"
#include "os/types.h"

namespace mister::hal {

struct FpgaAperture {
    PhysRegion region{};
    std::uint32_t mask = 0;

    [[nodiscard]] constexpr std::optional<os::PhysAddr> translate(
        std::uint32_t off) const noexcept {
        if (region.len == 0) return std::nullopt;
        return os::PhysAddr{region.phys.v | (off & mask)};
    }
};

}  // namespace mister::hal
