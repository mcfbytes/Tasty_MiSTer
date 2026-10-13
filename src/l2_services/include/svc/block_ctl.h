// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/storage_completion.h"
#include "proto/storage_request.h"
#include "svc/storage_backend.h"

namespace mister::svc {

struct BlockCtl {
    TASTY_SEAT_MEDIATOR(Any, Any);

    proto::StorageRequest ask{};
    proto::StorageCompletion answer{};
    std::uint32_t gen = 0;

    infra::OptRef<IStorageBackend> backend{};

    infra::OptRef<IStorageBackend> stage{};
};

static_assert(std::is_trivially_copyable_v<BlockCtl> &&
              sizeof(BlockCtl) == 72u + 2u * sizeof(infra::OptRef<IStorageBackend>));

}  // namespace mister::svc
