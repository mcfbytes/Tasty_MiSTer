// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "proto/types.h"

namespace mister::proto {

struct ItemProbe {
    static constexpr std::uint8_t kMaxWidth = 8;
    StatusBit lo{};
    std::uint8_t width = 0;
    std::uint8_t active_value = 0;
    friend constexpr bool operator==(ItemProbe, ItemProbe) = default;
};

static_assert(std::is_trivially_copyable_v<ItemProbe>);

}
