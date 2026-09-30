// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

struct PllParams {
    std::uint32_t c = 0;
    std::uint32_t m = 0;
    std::uint32_t k = 0;
    bool approximated = false;
};

}  // namespace mister::svc
