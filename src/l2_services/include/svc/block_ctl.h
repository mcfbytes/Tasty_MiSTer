// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

#include "infra/seat.h"
#include "proto/storage_completion.h"
#include "proto/storage_request.h"

namespace mister::svc {

struct BlockCtl {
    TASTY_SEAT_MEDIATOR(Any, Any);

    proto::StorageRequest ask{};
    proto::StorageCompletion answer{};
};

static_assert(std::is_trivially_copyable_v<BlockCtl> && sizeof(BlockCtl) == 64u);

}  // namespace mister::svc
