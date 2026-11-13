// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "ui/launch_slot.h"
#include "ui/text_entry_request.h"

namespace mister::ui {

struct TextEntryLaunch : LaunchSlot<TextEntryRequest> {
    TASTY_SEAT_RESIDENT(Ui);
};

}
