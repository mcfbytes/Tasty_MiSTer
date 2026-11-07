// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "ui/confirm_page.h"
#include "ui/launch_slot.h"

namespace mister::ui {

struct ConfirmLaunch : LaunchSlot<ConfirmPage::Spec> {
    TASTY_SEAT_RESIDENT(Ui);
};

}
