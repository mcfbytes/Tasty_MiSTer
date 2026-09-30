// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace mister::cores {

struct SavePartition {
    std::string_view name{};
    std::string_view suffix{};
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    std::uint8_t index = 0;
};

}  // namespace mister::cores
