// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/addon_send.h"
#include "app/file_tx_pieces.h"
#include "app/load_ladder.h"
#include "app/load_walk.h"
#include "infra/seat.h"

namespace mister::app {

struct BeltOrders {
    TASTY_SEAT_EXEMPT(component);
    std::optional<AddonSend> addons{};
    std::optional<AddonSend> addons_after{};
    std::optional<FileTxPieces> pieces{};
    std::optional<LoadLadder> load{};
    std::optional<LoadWalk> walk{};
};

}
