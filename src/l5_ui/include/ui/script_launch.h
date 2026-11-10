// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "ui/launch_slot.h"
#include "ui/script_request.h"

namespace mister::ui {

struct ScriptLaunch : LaunchSlot<ScriptRequest> {
    TASTY_SEAT_RESIDENT(Ui);
    using Request = ScriptRequest;
};

}
