// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>

namespace mister::cores {

struct RomsetAlias {
    bool drop = false;
    std::optional<std::string> alias;
};

}  // namespace mister::cores
