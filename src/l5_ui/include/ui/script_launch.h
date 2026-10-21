// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>

#include "infra/seat.h"
#include "ui/script_kind.h"

namespace mister::ui {

struct ScriptLaunch {
    TASTY_SEAT_RESIDENT(Ui);

    struct Request {
        ScriptKind kind = ScriptKind::Script;
        std::string argv0;
        bool bt_reset = false;
    };

    std::optional<Request> pending;
};

}
