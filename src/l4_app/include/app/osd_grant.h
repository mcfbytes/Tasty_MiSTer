// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/block_grant.h"
#include "hal/osd_block.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "proto/types.h"
#include "infra/prelude.h"

namespace mister::app {

struct OsdGrantBody {
    proto::OsdRowCount rows{hal::OsdBlock::kImageRows};

    [[nodiscard]] bool live() const noexcept { return true; }
};

struct OsdGrant : BlockGrant<OsdGrantBody> {};

static_assert(BlockGrantRecord<OsdGrant>);
using OsdGrantCell = xthread::Telemetry<OsdGrant, SeatTag::Main>;

}
