// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/lw_grant_stamp.h"
#include "app/osd_grant.h"
#include "hal/ocram.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::app {

struct OcramOsd {
    TASTY_SEAT_EXEMPT(component);
    using Grant = OsdGrant;
    hal::ocram::RingLend ring{};
    proto::OsdRowCount rows{};
    LwGrantStamp stamp{};

    [[nodiscard]] Grant lend() const noexcept {
        return Grant{{.block = {}, .gen = stamp.gen(), .body = {.rows = rows, .ring = ring}}};
    }
};

}
