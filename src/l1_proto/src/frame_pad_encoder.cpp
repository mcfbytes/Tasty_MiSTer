// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/frame_pad_encoder.h"

namespace mister::proto {

Ex<void> FramePadEncoder::digital(std::uint16_t opcode, JoyMask map) {
    TASTY_SEAT_BODY(FramePadEncoder);
    if (!frame_)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), opcode});
    ILinkFrame& frame = *frame_;
    if (auto e = frame.begin(FrameSel::Uio, opcode); !e)
        return e;
    if (auto e = frame.word(static_cast<std::uint16_t>(map.v & 0xFFFFu)); !e)
        return e;

    if (auto e = frame.word(static_cast<std::uint16_t>(map.v >> 16)); !e)
        return e;
    return frame.end();
}

Ex<void> FramePadEncoder::stick(
    std::uint16_t opcode,
    std::uint8_t index,
    std::uint8_t x,
    std::uint8_t y
) {
    TASTY_SEAT_BODY(FramePadEncoder);
    if (!frame_)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), opcode});
    ILinkFrame& frame = *frame_;
    if (auto e = frame.begin(FrameSel::Uio, opcode); !e)
        return e;
    if (auto e = frame.word(index); !e)
        return e;

    if (port_.wire().analog_word) {

        const auto w = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(y) << 8)
            | static_cast<std::uint16_t>(x)
        );
        if (auto e = frame.word(w); !e)
            return e;
        return frame.end();
    }

    if (auto e = frame.word(x); !e)
        return e;
    if (auto e = frame.word(y); !e)
        return e;
    return frame.end();
}

std::uint32_t FramePadEncoder::link_words(std::uint16_t, bool) const noexcept {
    TASTY_SEAT_BODY(FramePadEncoder);
    return JoystickPort::kMaxDigitalPushWords;
}

}
