// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "infra/seat.h"
#include "ui/launch_slot.h"
#include "ui/script_kind.h"

namespace mister::ui {

struct ScriptRequest {
    ScriptKind kind = ScriptKind::Script;
    std::string argv0;
    bool bt_reset = false;
};

struct ScriptLaunch : LaunchSlot<ScriptRequest> {
    TASTY_SEAT_RESIDENT(Ui);
    using Request = ScriptRequest;
};

}
