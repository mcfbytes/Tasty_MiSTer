// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

#include "infra/seat.h"
#include "svc/bt_activity.h"
#include "svc/bt_failure.h"
#include "svc/bt_presence.h"
#include "svc/view_gen.h"

namespace mister::svc {

struct BtView {
    TASTY_SEAT_EXEMPT(component);

    ViewGen gen{};
    BtPresence presence = BtPresence::NoBus;
    BtActivity activity{};
    BtFailure failure = BtFailure::None;
};

static_assert(std::is_trivially_copyable_v<BtView>);

}
