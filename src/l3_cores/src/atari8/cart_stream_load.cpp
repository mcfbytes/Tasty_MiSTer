// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/atari8/cart_stream_load.h"

#include <optional>

#include "cores/atari8/cart_slot.h"

namespace mister::cores::atari8 {

Ex<proto::SessionParams> CartStreamLoad::stream_opening(const StreamAsk& ask) {
    TASTY_SEAT_BODY(CartStreamLoad);
    Ex<proto::SessionParams> params = host_.stream_opening(ask);
    if (!params || !cart_slot_of(machine_, ask.index.io()))
        return params;
    params->ext = {};
    switch (machine_) {
        case Atari8Machine::Computer:
            break;
        case Atari8Machine::Console:
            params->aux = kConsoleCartStart;
            break;
    }
    return params;
}

void CartStreamLoad::stream_closed(const StreamLoadEnd& end) noexcept {
    TASTY_SEAT_BODY(CartStreamLoad);
    host_.stream_closed(end);
}

}
