// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string>
#include <string_view>

namespace mister::cores {

struct LadderCarry {
    std::string_view last_dir{};
    bool noreset = false;
    std::span<const std::string> slot_paths{};
};

}
