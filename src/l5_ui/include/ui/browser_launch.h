// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "ui/browser_session.h"
#include "ui/launch_slot.h"

namespace mister::ui {

struct BrowserLaunch : LaunchSlot<BrowserSession::Request> {
    TASTY_SEAT_RESIDENT(Ui);
};

}
