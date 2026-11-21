// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "infra/prelude.h"
#include "proto/conf_switches.h"
#include "proto/link_frame.h"
#include "proto/uio_opcodes.h"

namespace mister::svc {

[[nodiscard]] inline Ex<void> emit_but_sw(proto::ILinkFrame& frame, proto::ConfSwitches map) {

    if (auto r = frame.begin(proto::UioOp::ButSw); !r)
        return r;
    if (auto r = frame.word(map.word()); !r)
        return r;
    return frame.end();
}

}
