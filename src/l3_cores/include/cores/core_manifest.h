// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace mister::cores {

struct CoreManifest {
    std::string_view path{};
    std::string_view text{};
};

}  // namespace mister::cores
