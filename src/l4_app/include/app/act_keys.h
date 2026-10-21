// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::app {

class ActKeys final {
    TASTY_SEAT_RESIDENT(Main);

public:
    [[nodiscard]] std::uint32_t next() noexcept {
        ++gen_;
        if (gen_ == 0)
            gen_ = 1;
        return gen_;
    }

private:
    std::uint32_t gen_ = 0;
};

}
