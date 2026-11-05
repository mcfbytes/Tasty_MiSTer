// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "cores/nvram_store.h"
#include "cores/types.h"

namespace mister::cores::n64 {

struct SaveOrder {
    SavePath path{};

    std::uint32_t offset = 0;

    std::uint32_t create = 0;
    bool cpak = false;
    SaveRow row{};
};

static_assert(std::is_trivially_copyable_v<SaveOrder>);

static_assert(sizeof(SaveOrder) == 272 && alignof(SaveOrder) == 4);

}
