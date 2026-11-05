// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "proto/types.h"

namespace mister::app {

template <class T>
struct Answered {
    TASTY_SEAT_EXEMPT(component);
    proto::ActKey gen{};
    T value{};
};

}
