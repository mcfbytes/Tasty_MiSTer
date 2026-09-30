// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"

namespace mister::reactor {

class ICoreToken;

struct CoreState {
    TASTY_SEAT_RESIDENT(RT);
    ICoreToken* owner = nullptr;
};

}  // namespace mister::reactor
