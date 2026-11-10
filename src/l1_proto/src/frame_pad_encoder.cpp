// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/frame_pad_encoder.h"

namespace mister::proto {

Ex<void> FramePadEncoder::digital(UioOp opcode, JoyMask map) {
    TASTY_SEAT_BODY(FramePadEncoder);
    if (!frame_)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), proto::word(opcode).v});
    ILinkFrame& frame = *frame_;
    if (auto e = frame.begin(opcode); !e)
        return e;
    if (auto e = frame.word(static_cast<std::uint16_t>(map.v)); !e)
        return e;

    if (auto e = frame.word(static_cast<std::uint16_t>(map.v >> 16)); !e)
        return e;
    return frame.end();
}

Ex<void> FramePadEncoder::stick(UioOp opcode, std::uint8_t index, StickPos pos) {
    TASTY_SEAT_BODY(FramePadEncoder);
    if (!frame_)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), proto::word(opcode).v});
    ILinkFrame& frame = *frame_;
    if (auto e = frame.begin(opcode); !e)
        return e;
    if (auto e = frame.word(index); !e)
        return e;

    if (port_.wire().analog_word) {

        if (auto e = frame.word(pos.packed()); !e)
            return e;
        return frame.end();
    }

    if (auto e = frame.word(pos.x); !e)
        return e;
    if (auto e = frame.word(pos.y); !e)
        return e;
    return frame.end();
}

std::uint32_t FramePadEncoder::link_words(UioOp, bool) const noexcept {
    TASTY_SEAT_BODY(FramePadEncoder);
    return JoystickPort::kMaxDigitalPushWords;
}

}
