// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>

#include "cores/n64_rom.h"
#include "infra/seat.h"

namespace mister::cores::n64 {

class HalfwordSwap final : public IWordOrder {
    TASTY_SEAT_RESIDENT(Main);

public:
    constexpr void normalize(std::span<std::uint8_t> data) const noexcept override {
        if (!std::is_constant_evaluated())
            TASTY_SEAT_BODY(HalfwordSwap);
        for (std::size_t i = 0; i + 2 <= data.size(); i += 2) {
            std::swap(data[i], data[i + 1]);
        }
    }
};

}
