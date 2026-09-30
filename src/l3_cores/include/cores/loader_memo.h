// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "svc/slot_path.h"

namespace mister::cores {

struct LoaderMemo {
    TASTY_SEAT_EXEMPT(main);

    FixedStr<svc::kSlotPathCap, StrFit::Reject> last_boot{};
};

}  // namespace mister::cores
