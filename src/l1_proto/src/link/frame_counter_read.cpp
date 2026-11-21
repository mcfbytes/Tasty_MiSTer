// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/frame_counter_read.h"

#include "infra/prelude.h"
#include "proto/uio_opcodes.h"

namespace mister::proto::link {

Ex<FrameCount> FrameCounterRead::read(Link& link) {
    ++reads_;
    const auto r = link.command(UioOp::GetFrCnt);
    if (!r)
        return std::unexpected(r.error());
    link.close();
    return FrameCount{.supported = r->intersects(kSupportedBit), .count = r->low_byte()};
}

}
