// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/input/link_pad_sink.h"

#include "app/link_tx_channel.h"
#include "proto/link_op.h"

namespace mister::app::input {

bool LinkPadSink::emit(svc::PlayerIndex player, proto::PadMasks masks) noexcept {
    TASTY_SEAT_BODY(LinkPadSink);
    return inbox_.push(
        proto::LinkOp::JoyEmit{.player = player, .mask = masks.buttons, .autofire = masks.autofire}
    );
}

bool LinkPadSink::release(svc::PlayerIndex player, svc::JoyMask tested) noexcept {
    TASTY_SEAT_BODY(LinkPadSink);
    return inbox_.push(proto::LinkOp::JoyRelease{.player = player, .mask = tested});
}

bool LinkPadSink::stick(
    svc::PlayerIndex player,
    proto::StickSide side,
    proto::StickPos pos
) noexcept {
    TASTY_SEAT_BODY(LinkPadSink);
    return inbox_.push(proto::LinkOp::JoyStick{.player = player, .side = side, .pos = pos});
}

}
