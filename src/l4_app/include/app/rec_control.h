// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include <type_traits>

#include "app/rec_options.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

enum class RecOp : std::uint8_t { None, Start, Stop, Arm, Disarm };

enum class RecMode : std::uint8_t { Hash, Avi };

using RecPath = FixedStr<256, StrFit::Reject>;

struct RecControl {
    std::uint16_t gen = 0;
    RecOp op = RecOp::None;
    RecMode mode = RecMode::Hash;
    RecPath path{};
    RecOptions opt{};
};

static_assert(std::is_trivially_copyable_v<RecControl>);
static_assert(std::is_nothrow_default_constructible_v<RecControl>);

using RecControlCell = xthread::Telemetry<RecControl, SeatTag::Ui>;

}  // namespace mister::app
