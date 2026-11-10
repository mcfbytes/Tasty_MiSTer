// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "cores/save_digit.h"

namespace mister::cores {

struct SavePartition {
    std::string_view name{};
    std::string_view suffix{};
    std::uint32_t offset = 0;

    std::uint32_t length = 0;

    std::optional<SaveDigit> digit{};
};

}
