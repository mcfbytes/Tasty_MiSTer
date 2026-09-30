// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "proto/types.h"

namespace mister::cores {

struct LoadAsk {
    proto::IoIndex index{};
    std::string_view path{};
    std::uint64_t size = 0;
    std::uint32_t load_addr = 0;

    bool disc = false;
};

}  // namespace mister::cores
