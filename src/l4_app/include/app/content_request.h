// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "app/path_text.h"
#include "cores/ram_image.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::app {

struct ContentRequest {
    TASTY_SEAT_EXEMPT(component);
    PathText path{};
    std::uint8_t slot = 0;
    std::uint32_t load_addr = 0;
    proto::FileId save{};
    cores::RamImageRecipe ram_image{};
    std::uint8_t ram_index = 0;
};
static_assert(std::is_trivially_copyable_v<ContentRequest>,
              "the path rides the request, never a PathId");

}  // namespace mister::app
