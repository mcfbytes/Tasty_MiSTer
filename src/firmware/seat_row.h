// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"

namespace mister::fw {

template <SeatTag S, class M>
struct SeatRow {
    static constexpr SeatTag seat = S;
    using Main = M;
};

}
