// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/block_grant.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "proto/joy_bits.h"

namespace mister::app {

struct PadGrantBody {
    proto::JoystickWire wire{};

    [[nodiscard]] bool live() const noexcept { return true; }
};

struct PadGrant : BlockGrant<PadGrantBody> {};

static_assert(BlockGrantRecord<PadGrant>);
using PadGrantCell = xthread::Telemetry<PadGrant, SeatTag::Main>;

}
