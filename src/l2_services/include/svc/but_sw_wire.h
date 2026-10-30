// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "proto/link_frame.h"
#include "proto/uio_opcodes.h"

namespace mister::svc {

[[nodiscard]] inline Ex<void> emit_but_sw(proto::ILinkFrame& frame, std::uint16_t map) {

    if (auto r = frame.begin(proto::UioOp::ButSw); !r)
        return r;
    if (auto r = frame.word(map); !r)
        return r;
    return frame.end();
}

}
