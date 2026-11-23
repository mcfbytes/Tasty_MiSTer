// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/block_geometry.h"
#include "proto/types.h"

namespace mister::proto {

class IWindowGeometry {
public:
    virtual ~IWindowGeometry() = default;

    [[nodiscard]] virtual BlockGeometry geometry_for(
        SlotIndex slot,
        Lba lba,
        BlockGeometry wire
    ) const noexcept = 0;

protected:
    IWindowGeometry() = default;
    IWindowGeometry(const IWindowGeometry&) = default;
    IWindowGeometry& operator=(const IWindowGeometry&) = default;
};

}
