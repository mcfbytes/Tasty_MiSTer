// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace mister::hal {

struct KernelContract {
    std::string_view doorbell_prefix;
    std::string_view sd_block;
};

}  // namespace mister::hal
