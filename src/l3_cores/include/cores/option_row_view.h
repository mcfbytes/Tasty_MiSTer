// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace mister::cores {

struct OptionRowView {

    std::uint8_t current = 0;
    bool dimmed = false;
    std::string_view text{};
};

}  // namespace mister::cores
