// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/held_pad_encoder.h"

namespace mister::proto {

Ex<void> HeldPadEncoder::digital(std::uint16_t opcode, JoyMask map) {
    TASTY_SEAT_BODY(HeldPadEncoder);
    return hold_(opcode, 0, map.v);
}

Ex<void> HeldPadEncoder::stick(
    std::uint16_t opcode,
    std::uint8_t index,
    std::uint8_t x,
    std::uint8_t y
) {
    TASTY_SEAT_BODY(HeldPadEncoder);
    return hold_(opcode, index, PadLevels::stick_word(x, y));
}

std::uint32_t HeldPadEncoder::link_words(std::uint16_t, bool) const noexcept {
    TASTY_SEAT_BODY(HeldPadEncoder);
    return 0;
}

Ex<void> HeldPadEncoder::release(ILinkPadEncoder& to) const {
    TASTY_SEAT_BODY(HeldPadEncoder);
    Ex<void> first{};
    for (std::size_t i = 0; i < PadLevels::kCount; ++i)
        if (auto r = want_.replay(i, to); !r && first)
            first = r;
    return first;
}

Ex<void> HeldPadEncoder::hold_(std::uint16_t opcode, std::uint8_t index, std::uint32_t word) {
    const std::optional<std::size_t> s = PadLevels::slot(opcode, index);
    if (!s)
        return std::unexpected(Error{Errc::fpga_not_ready, ERR_SITE(), opcode});
    want_.put(*s, word);
    ++held_;
    return {};
}

}
