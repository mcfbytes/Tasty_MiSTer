// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

#include "infra/seat.h"
#include "svc/connect/bt_activity.h"
#include "svc/connect/bt_failure.h"
#include "svc/connect/bt_presence.h"
#include "svc/connect/view_gen.h"

namespace mister::svc::connect {

struct BtView {
    TASTY_SEAT_EXEMPT(component);

    ViewGen gen{};
    BtPresence presence = BtPresence::NoBus;
    BtActivity activity{};
    BtFailure failure = BtFailure::None;
};

static_assert(std::is_trivially_copyable_v<BtView>);

}
