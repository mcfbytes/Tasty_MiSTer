// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace mister::svc {

struct FilterSendPlan {
    struct Seg {
        bool vert = false;
        bool adaptive = false;
        std::uint8_t bank = 0;
    };
    std::array<Seg, 3> segs{};
    std::uint8_t count = 0;
};

}  // namespace mister::svc
