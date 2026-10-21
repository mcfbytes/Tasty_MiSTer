// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "cores/n64_save_type.h"
#include "infra/seat.h"
#include "svc/slot_path.h"
#include "svc/storage_backend.h"

namespace mister::cores::n64 {

struct SaveOrder {
    TASTY_SEAT_EXEMPT(component);
    svc::SlotPathText path{};

    svc::SlotPathText stock{};
    std::uint32_t offset = 0;

    std::uint32_t create = 0;
    bool cpak = false;

    std::uint32_t declared[kMaxSaveRows]{};
    bool byteswap[kMaxSaveRows]{};
};

static_assert(
    std::is_trivially_copyable_v<SaveOrder> && sizeof(SaveOrder) <= svc::kStorageOrderBytes
);

static_assert(sizeof(SaveOrder) == 2096);
static_assert(alignof(SaveOrder) == 4);

}
