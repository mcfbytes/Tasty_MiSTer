// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/frame_pad_encoder.h"

#include "infra/prelude.h"

namespace mister::proto::link {

Ex<void> FramePadEncoder::digital(UioOp opcode, JoyMask map) {
    TASTY_SEAT_BODY(FramePadEncoder);
    if (!frame_)
        return std::unexpected(proto::word(opcode).refusal(Errc::not_found, ERR_SITE()));
    ILinkFrame& frame = *frame_;
    if (auto e = frame.begin(opcode); !e)
        return e;
    const hal::LinkHalves halves = map.halves();
    if (auto e = frame.word(halves.low); !e)
        return e;

    if (auto e = frame.word(halves.high); !e)
        return e;
    return frame.end();
}

Ex<void> FramePadEncoder::stick(UioOp opcode, std::uint8_t index, StickPos pos) {
    TASTY_SEAT_BODY(FramePadEncoder);
    if (!frame_)
        return std::unexpected(proto::word(opcode).refusal(Errc::not_found, ERR_SITE()));
    ILinkFrame& frame = *frame_;
    if (auto e = frame.begin(opcode); !e)
        return e;
    if (auto e = frame.word(hal::LinkWord{index}); !e)
        return e;

    if (port_.wire().analog_word) {

        if (auto e = frame.word(hal::LinkWord{pos.packed()}); !e)
            return e;
        return frame.end();
    }

    if (auto e = frame.word(hal::LinkWord{pos.x}); !e)
        return e;
    if (auto e = frame.word(hal::LinkWord{pos.y}); !e)
        return e;
    return frame.end();
}

std::uint32_t FramePadEncoder::link_words(UioOp, bool) const noexcept {
    TASTY_SEAT_BODY(FramePadEncoder);
    return JoystickPort::kMaxDigitalPushWords;
}

}
