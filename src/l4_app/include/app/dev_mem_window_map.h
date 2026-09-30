// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/load_window_map.h"
#include "infra/seat.h"

namespace mister::app {

class DevMemWindowMap final : public ILoadWindowMap {
    TASTY_SEAT_EXEMPT(main);

public:
    [[nodiscard]] Ex<hal::FpgaMemory> map(const hal::PhysRegion& r) override {
        return hal::FpgaMemory::map(r);
    }
};

}  // namespace mister::app
