// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/path_text.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "proto/types.h"

namespace mister::cores {

struct RemountAsked {
    proto::SlotIndex slot{};
    infra::PathText path{};
};

using RemountCell = xthread::Telemetry<RemountAsked, SeatTag::Io>;

}
