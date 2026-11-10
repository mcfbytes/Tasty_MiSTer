// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

#include "infra/indexed_array.h"
#include "svc/pad_signature.h"
#include "svc/types.h"

namespace mister::svc {

struct RumblePadSet {
    PlayerMask players{};
    infra::IndexedArray<PadSignature, kMaxPlayers, PlayerIndex> pads{};

    friend constexpr bool operator==(const RumblePadSet& a, const RumblePadSet& b) {
        return a.players == b.players && std::equal(a.pads.begin(), a.pads.end(), b.pads.begin());
    }
};

}
