// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "hal/fpga_memory.h"
#include "hal/phys_region.h"

namespace mister::app {

class ILoadWindowMap {
public:
    virtual ~ILoadWindowMap() = default;
    [[nodiscard]] virtual Ex<hal::FpgaMemory> map(const hal::PhysRegion& r) = 0;

protected:
    ILoadWindowMap() = default;
    ILoadWindowMap(const ILoadWindowMap&) = default;
    ILoadWindowMap& operator=(const ILoadWindowMap&) = default;
};

}  // namespace mister::app
