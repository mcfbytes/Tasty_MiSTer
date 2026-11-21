// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/xthread/seat_park.h"

namespace mister::infra::xthread {

struct NeverPark {
    void arm() noexcept {}

    void wait(infra::ParkTimeout) noexcept {}

    void disarm() noexcept {}
};

static_assert(ParkLike<NeverPark>);

}
