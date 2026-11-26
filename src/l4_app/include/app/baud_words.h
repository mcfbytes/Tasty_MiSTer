// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/baud_halves.h"
#include "cores/types.h"
#include "hal/link_halves.h"

namespace mister::app {

class UartModeController;

class BaudWords {
public:
    [[nodiscard]] static constexpr BaudWords of(cores::BaudRate baud) noexcept {
        return BaudWords{baud.halves()};
    }

private:
    explicit constexpr BaudWords(hal::LinkHalves halves) noexcept : halves_(halves) {}

    friend class UartModeController;
    hal::LinkHalves halves_{};
};

}
