// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "ui/confirm_page.h"

namespace mister::ui {

struct ConfirmLaunch {
    TASTY_SEAT_RESIDENT(Ui);
    std::optional<ConfirmPage::Spec> pending;
};

}
