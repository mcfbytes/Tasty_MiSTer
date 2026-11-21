// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/xthread/inbox.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "proto/save_stage.h"

namespace mister::svc {

using SaveStageOrders = xthread::Inbox<proto::SaveStageOrder, 16>;

using SaveStageCell = xthread::Telemetry<proto::SaveStageReport, SeatTag::Io>;

}
