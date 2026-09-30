// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/status_register.h"
#include "proto/status_word.h"

namespace mister::proto {

static_assert(sizeof(StatusWord) == StatusRegister::kWords * sizeof(std::uint16_t),
              "the cell IS the register's word array; a padded or resized "
              "payload would silently drop the high bits the cell exists for");
static_assert(StatusWord::kWords == StatusRegister::kWords);
static_assert(std::is_trivially_copyable_v<StatusWord>);

using StatusCell = xthread::Telemetry<StatusWord, SeatTag::RT>;

}  // namespace mister::proto
