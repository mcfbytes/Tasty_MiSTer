// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/block_geometry.h"
#include "proto/resident_window.h"
#include "proto/storage_completion.h"
#include "proto/storage_request.h"
#include "svc/storage_backend.h"
#include "svc/types.h"

namespace mister::svc {

struct SlotArena {
    TASTY_SEAT_MEDIATOR(Any, Any);

    proto::StorageRequest ask{};
    proto::StorageCompletion answer{};
    Generation gen{};
    infra::OptRef<IStorageBackend> backend{};
    proto::ResidentWindow window{};
    alignas(64) std::array<std::uint8_t, proto::kBlockStagingBytes> bytes{};
};

static_assert(std::is_trivially_copyable_v<SlotArena>);
static_assert(sizeof(SlotArena) == 128u + proto::kBlockStagingBytes, "one window per slot");

}
