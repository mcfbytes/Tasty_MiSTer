// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::cores {

class ScsiSense {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Block {
        std::uint8_t key = 0;
        std::uint8_t asc = 0;
        std::uint8_t ascq = 0;
        std::uint8_t fru = 0;
    };

    void raise(std::uint8_t key, std::uint8_t asc) noexcept { block_ = Block{key, asc, 0, 0}; }

    [[nodiscard]] Block take() noexcept {
        const Block out = block_;
        block_ = Block{};
        return out;
    }

private:
    Block block_{};
};

}
