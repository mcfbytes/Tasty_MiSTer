// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/content_request.h"
#include "app/types.h"
#include "app/ui_request.h"
#include "infra/seat.h"

namespace mister::app {

struct PickedLoad {
    TASTY_SEAT_EXEMPT(component);
    ContentRequest content{};
    bool opensave = false;
    UiRequest::SaveChoice choice = UiRequest::SaveChoice::User;
    CorrelationTag tag{};
};

}
