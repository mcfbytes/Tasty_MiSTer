// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/input/pad_level_sink.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/pad_masks.h"
#include "svc/types.h"

namespace mister::app {
template <SeatTag Seat>
class LinkTxChannel;
}

namespace mister::app::input {

class LinkPadSink final : public IPadLevelSink {
    TASTY_SEAT_RESIDENT(Input);

public:
    explicit LinkPadSink(LinkTxChannel<SeatTag::Input>& inbox) noexcept : inbox_(inbox) {}

    LinkPadSink(const LinkPadSink&) = delete;
    LinkPadSink& operator=(const LinkPadSink&) = delete;

    [[nodiscard]] bool emit(svc::PlayerIndex player, proto::PadMasks masks) noexcept override;
    [[nodiscard]] bool release(svc::PlayerIndex player, svc::JoyMask tested) noexcept override;
    [[nodiscard]] bool stick(
        svc::PlayerIndex player,
        proto::StickSide side,
        proto::StickPos pos
    ) noexcept override;

private:
    LinkTxChannel<SeatTag::Input>& inbox_;
};

}
