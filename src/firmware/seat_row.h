// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::fw {

enum class Presence : std::uint8_t { Required, ConfigDependent };

template <SeatTag S, class M, Presence P = Presence::Required>
struct SeatRow {
    static constexpr SeatTag seat = S;
    using Main = M;
    static constexpr Presence presence = P;
};

}
