// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/log_clock.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"

namespace mister::app {

struct PadRelayTally {
    TASTY_SEAT_RESIDENT(RT);
    infra::Tally publishes{};
    infra::Tally acked{};
    infra::Tally superseded{};
    infra::Tally unbound{};
    StatusMicros lag_last{};
    StatusMicros lag_max{};
    bool input_gated = false;
};

}
