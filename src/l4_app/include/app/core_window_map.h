// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/load_window_map.h"
#include "hal/fabric_region.h"
#include "hal/fpga_memory.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/seat_owner_main.h"
#include "os/types.h"

namespace mister::app {

class CoreWindowMap final {
    TASTY_SEAT_RESIDENT(Main);

public:
    CoreWindowMap(
        const hal::FpgaAperture& aperture,
        ILoadWindowMap<seat_owner::Main>& maps
    ) noexcept
        : aperture_(aperture),
          maps_(maps) {}

    [[nodiscard]] Ex<hal::FpgaMemory> map(const hal::FabricRegion& row) {
        const auto region = aperture_.region_of(row);
        if (!region)
            return std::unexpected(region.error());
        return maps_.map(*region);
    }

private:
    const hal::FpgaAperture& aperture_;
    ILoadWindowMap<seat_owner::Main>& maps_;
};

}
