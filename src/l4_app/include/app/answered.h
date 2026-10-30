// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::app {

template <class T>
struct Answered {
    TASTY_SEAT_EXEMPT(component);
    std::uint32_t gen = 0;
    T value{};
};

}
