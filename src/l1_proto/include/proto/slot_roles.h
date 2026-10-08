// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/block_geometry.h"
#include "proto/block_geometry_hook.h"
#include "proto/image_source.h"
#include "proto/resident_image_source.h"

namespace mister::proto {

struct SlotRoles {
    TASTY_SEAT_RESIDENT(RT);

    infra::OptRef<IResidentImageSource> resident{};
    infra::OptRef<IImageSource> descriptor{};
    infra::OptRef<IBlockGeometry> geometry{};
};

using SlotRoleTable = std::array<SlotRoles, kBlockSlots>;

}  // namespace mister::proto
