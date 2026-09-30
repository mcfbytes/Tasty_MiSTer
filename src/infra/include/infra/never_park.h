// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat_park.h"

namespace mister::xthread {

struct NeverPark {
    void arm() noexcept {}
    void wait(int) noexcept {}
    void disarm() noexcept {}
};

static_assert(ParkLike<NeverPark>);

}  // namespace mister::xthread
