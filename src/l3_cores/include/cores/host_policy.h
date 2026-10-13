// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/pause_on_osd.h"
#include "infra/seat.h"

namespace mister::cores {

struct HostPolicy {
    TASTY_SEAT_EXEMPT(const_shared);
    PauseOnOsd pause = PauseOnOsd::Option;
};

}  // namespace mister::cores
