// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/phys_region.h"
#include "hal/types.h"

namespace mister::proto {

struct SaveStateDecl {

    static constexpr std::uint32_t kMaxBytes = 128u * 1024u * 1024u;

    hal::FabricAddr base;
    std::uint32_t size;

    [[nodiscard]] constexpr bool within(hal::PhysRegion aperture) const noexcept {
        if (size == 0u || size > kMaxBytes)
            return false;
        const std::optional<hal::FabricAddr> low = hal::FabricAddr::of(aperture.phys);
        const std::optional<hal::FabricAddr> high = hal::FabricAddr::of(aperture.end());

        if (!low || !high || base < *low)
            return false;
        const hal::FabricAddr last = base.advanced(size);

        return last >= base && last < *high;
    }
};

}
