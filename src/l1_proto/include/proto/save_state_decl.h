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
        const std::optional<hal::FabricAddr> low = hal::to_fabric(aperture.phys);
        const os::PhysAddr end{aperture.phys.v + std::uint64_t{aperture.len}};

        if (!low || !hal::to_fabric(end))
            return false;
        const os::PhysAddr at = hal::to_phys(base);
        return base >= *low && at < end && at.advanced(size) < end;
    }
};

}
