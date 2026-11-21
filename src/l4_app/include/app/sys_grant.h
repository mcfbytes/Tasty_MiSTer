// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/block_grant.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "os/uio_line_ref.h"

namespace mister::app {

struct SysGrantBody {
    os::UioLineRef line{};

    [[nodiscard]] bool live() const noexcept { return line.fd() >= 0; }
};

struct SysGrant : BlockGrant<SysGrantBody> {};

static_assert(BlockGrantRecord<SysGrant>);
using SysGrantCell = xthread::Telemetry<SysGrant, SeatTag::Main>;

}
