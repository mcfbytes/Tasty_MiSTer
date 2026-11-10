// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/held_pad_encoder.h"

#include <utility>

namespace mister::proto {

Ex<void> HeldPadEncoder::digital(UioOp opcode, JoyMask map) {
    TASTY_SEAT_BODY(HeldPadEncoder);
    return hold_(opcode, 0, map.v);
}

Ex<void> HeldPadEncoder::stick(UioOp opcode, std::uint8_t index, StickPos pos) {
    TASTY_SEAT_BODY(HeldPadEncoder);
    return hold_(opcode, index, pos.packed());
}

std::uint32_t HeldPadEncoder::link_words(UioOp, bool) const noexcept {
    TASTY_SEAT_BODY(HeldPadEncoder);
    return 0;
}

Ex<void> HeldPadEncoder::release(ILinkPadEncoder& to) const {
    TASTY_SEAT_BODY(HeldPadEncoder);
    Ex<void> first{};
    for (std::size_t i = 0; i < PadLevels::kCount; ++i)
        if (auto r = want_.replay(PadLevels::slot_at(i), to); !r && first)
            first = r;
    return first;
}

Ex<void> HeldPadEncoder::hold_(UioOp opcode, std::uint8_t index, std::uint32_t word) {
    const PadLevels::Slot s = PadLevels::level_of(opcode, index);
    if (!PadLevels::is_level(s))
        return std::unexpected(Error{Errc::fpga_not_ready, ERR_SITE(), proto::word(opcode).v});
    want_.put(s, word);
    ++held_;
    return {};
}

}
