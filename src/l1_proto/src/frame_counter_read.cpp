// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/frame_counter_read.h"

#include "proto/uio_opcodes.h"

namespace mister::proto {

Ex<FrameCount> FrameCounterRead::read(Link& link) {
    ++reads_;
    const auto r = link.command(UioOp::GetFrCnt);
    if (!r)
        return std::unexpected(r.error());
    link.close();
    const std::uint16_t w = *r;
    return FrameCount{.supported = (w & 0x100u) != 0u, .count = static_cast<std::uint8_t>(w)};
}

}
