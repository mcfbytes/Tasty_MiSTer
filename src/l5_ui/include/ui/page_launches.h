// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::ui {

struct BrowserLaunch;
struct ConfirmLaunch;
struct ScriptLaunch;

struct PageLaunches {
    TASTY_SEAT_RESIDENT(Ui);

    infra::OptRef<BrowserLaunch> browser{};
    infra::OptRef<ScriptLaunch> script{};
    infra::OptRef<ConfirmLaunch> confirm{};
};

}
