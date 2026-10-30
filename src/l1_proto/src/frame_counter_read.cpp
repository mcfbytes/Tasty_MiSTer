// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/frame_counter_read.h"

#include "hal/selected.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

Ex<FrameCount> FrameCounterRead::read(hal::ISpiTransport& link) {
    ++reads_;
    hal::Selected cs(link, hal::ChipSelect::Io);
    const auto r = link.transfer(word(UioOp::GetFrCnt));
    if (!r)
        return std::unexpected(r.error());
    const std::uint16_t w = r->v;
    return FrameCount{
        .supported = (w & 0x100u) != 0u,
        .count = static_cast<std::uint8_t>(w & 0xFFu)
    };
}

}
