// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "proto/status_word.h"

namespace mister::proto {

static_assert(std::is_trivially_copyable_v<StatusWord>);

using StatusCell = xthread::Telemetry<StatusWord, SeatTag::RT>;

}
