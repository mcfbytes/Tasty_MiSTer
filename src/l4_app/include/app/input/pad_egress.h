// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/input/link_pad_sink.h"
#include "app/input/pad_level_sink.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "proto/pad_masks.h"
#include "svc/types.h"

namespace mister::app::input {

class LwPadEndpoint;

class PadEgress {
    TASTY_SEAT_RESIDENT(Input);

public:
    PadEgress(LinkTxChannel<SeatTag::Input>& inbox, infra::OptRef<LwPadEndpoint> lw) noexcept
        : link_(inbox),
          lw_(lw) {}

    PadEgress(const PadEgress&) = delete;
    PadEgress& operator=(const PadEgress&) = delete;

    [[nodiscard]] bool emit(svc::PlayerIndex player, proto::PadMasks masks) noexcept {
        return route_().emit(player, masks);
    }

    [[nodiscard]] bool release(svc::PlayerIndex player, svc::JoyMask tested) noexcept {
        return route_().release(player, tested);
    }

    [[nodiscard]] infra::Tally binds() const noexcept;

private:
    [[nodiscard]] IPadLevelSink& route_() noexcept;

    LinkPadSink link_;
    infra::OptRef<LwPadEndpoint> lw_;
};

}
