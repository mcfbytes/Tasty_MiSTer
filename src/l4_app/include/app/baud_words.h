// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/types.h"

namespace mister::app {

class UartModeController;

class BaudWords {
public:
    [[nodiscard]] static constexpr BaudWords of(cores::BaudRate baud) noexcept {
        return BaudWords{baud.low_word(), baud.high_word()};
    }

private:
    constexpr BaudWords(std::uint16_t lo, std::uint16_t hi) noexcept : lo_(lo), hi_(hi) {}

    friend class UartModeController;
    std::uint16_t lo_ = 0;
    std::uint16_t hi_ = 0;
};

}
