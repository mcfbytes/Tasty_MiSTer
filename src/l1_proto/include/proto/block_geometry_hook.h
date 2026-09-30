// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/block_geometry.h"
#include "proto/types.h"

namespace mister::proto {

class IBlockGeometry {
public:
    virtual ~IBlockGeometry() = default;

    virtual BlockGeometry geometry_for(SlotIndex slot, Lba lba, BlockGeometry wire) = 0;

protected:
    IBlockGeometry() = default;
    IBlockGeometry(const IBlockGeometry&) = default;
    IBlockGeometry& operator=(const IBlockGeometry&) = default;
};

}  // namespace mister::proto
