// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "ui/browser_session.h"

namespace mister::ui {

struct BrowserLaunch {
    TASTY_SEAT_RESIDENT(Ui);
    std::optional<BrowserSession::Request> pending;
};

}
