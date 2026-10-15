// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
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

    std::uint16_t order_len = 0;
    alignas(8) std::byte order[kStorageOrderBytes]{};

    infra::OptRef<IStorageBackend> stage{};
};

static_assert(std::is_trivially_copyable_v<BlockCtl> &&
              sizeof(BlockCtl) <= 96u + kStorageOrderBytes);

}  // namespace mister::svc
