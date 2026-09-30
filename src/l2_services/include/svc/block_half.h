// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "proto/block_geometry.h"
#include "proto/storage_completion.h"
#include "proto/storage_request.h"

namespace mister::svc {

struct BlockHalf {
    TASTY_SEAT_MEDIATOR(Any, Any);

    proto::StorageRequest ask{};
    proto::StorageCompletion answer{};
    alignas(64) std::uint8_t data[proto::kBlockStagingBytes]{};
};

static_assert(std::is_trivially_copyable_v<BlockHalf>);
static_assert(sizeof(BlockHalf) == 64u + proto::kBlockStagingBytes, "the half dominates");

}  // namespace mister::svc
