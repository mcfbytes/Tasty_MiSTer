// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "proto/block_geometry.h"
#include "proto/image_source.h"
#include "proto/types.h"

namespace mister::proto {

class IBlockServer : public IImageSource {
public:
    [[nodiscard]] virtual BlockGeometry geometry_for(SlotIndex, Lba, BlockGeometry wire) {
        return wire;
    }

protected:
    IBlockServer() = default;
    IBlockServer(const IBlockServer&) = default;
    IBlockServer& operator=(const IBlockServer&) = default;
};

}
