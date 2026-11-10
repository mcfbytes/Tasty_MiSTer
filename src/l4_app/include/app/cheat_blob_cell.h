// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "cores/cheat_geometry.h"
#include "cores/cheat_unit.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct CheatBlob {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint16_t len = 0;

    cores::CheatUnit unit{};
    std::uint8_t bytes[cores::kCheatTableBytes] = {};

    [[nodiscard]] std::span<const std::uint8_t> body() const noexcept {
        return std::span<const std::uint8_t>{bytes, len <= sizeof(bytes) ? len : sizeof(bytes)};
    }
};

static_assert(std::is_trivially_copyable_v<CheatBlob>);

using CheatBlobCell = xthread::Telemetry<CheatBlob, SeatTag::Ui>;

}
