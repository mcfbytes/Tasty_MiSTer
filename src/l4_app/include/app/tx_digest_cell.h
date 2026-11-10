// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::app {

struct TxDigest {
    TASTY_SEAT_EXEMPT(const_shared);

    enum class Kind : std::uint8_t { None, Load, Mount };

    static constexpr std::size_t kPathCap = 1024;

    proto::BindGeneration gen{};
    Kind kind = Kind::None;
    bool same_game = false;
    svc::Crc32 crc{};
    FixedStr<kPathCap, StrFit::Clip> path{};
};

static_assert(std::is_trivially_copyable_v<TxDigest>);

using TxDigestCell = xthread::Telemetry<TxDigest, SeatTag::RT>;

}
