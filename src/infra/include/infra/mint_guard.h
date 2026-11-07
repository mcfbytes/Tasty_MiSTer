// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::infra {

class MintGuard {
    TASTY_SEAT_EXEMPT(component);

public:
    enum class End : std::uint8_t { first = 1, second = 2 };

    [[nodiscard]] bool mint(End end) noexcept {
        const auto bit = static_cast<std::uint8_t>(end);
        if ((minted_ & bit) != 0)
            return false;
        minted_ = static_cast<std::uint8_t>(minted_ | bit);
        return true;
    }

private:
    std::uint8_t minted_ = 0;
};

}
