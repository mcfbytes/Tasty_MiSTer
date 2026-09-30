// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace mister::cores {

struct DipRowView {
    std::string_view name;
    std::uint8_t choices = 0;
    std::uint8_t current = 0;
};

}  // namespace mister::cores
