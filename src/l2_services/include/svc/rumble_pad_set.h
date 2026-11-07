// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "svc/types.h"

namespace mister::svc {

struct RumblePadSet {
    PlayerMask players{};
    std::array<std::uint32_t, kMaxPlayers> pads{};
    friend constexpr bool operator==(const RumblePadSet&, const RumblePadSet&) = default;
};

}
