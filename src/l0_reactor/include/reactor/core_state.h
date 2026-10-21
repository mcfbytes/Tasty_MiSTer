// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::reactor {

class ICoreToken;

struct CoreState {
    TASTY_SEAT_RESIDENT(RT);
    infra::OptRef<ICoreToken> owner{};
};

}
