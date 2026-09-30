// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct TxDigest {
    TASTY_SEAT_EXEMPT(const_shared);

    enum class Kind : std::uint8_t { None, Load, Mount };

    static constexpr std::size_t kPathCap = 1024;

    Kind kind = Kind::None;
    bool same_game = false;
    std::uint32_t crc = 0;
    FixedStr<kPathCap, StrFit::Clip> path{};
};
static_assert(std::is_trivially_copyable_v<TxDigest>);

using TxDigestCell = xthread::Telemetry<TxDigest, SeatTag::RT>;

}  // namespace mister::app
