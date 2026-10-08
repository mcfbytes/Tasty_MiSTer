// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct CheatCatalog {
    TASTY_SEAT_EXEMPT(const_shared);
    static constexpr std::size_t kMaxRows = 64;
    static constexpr std::size_t kNameChars = 127;
    static constexpr std::size_t kArenaBytes = 8192;

    struct Row {
        char name[kNameChars + 1] = {};
        std::uint16_t offset = 0;
        std::uint16_t len = 0;
    };

    std::uint32_t unit = 16;
    std::uint16_t max_active = 128;
    std::uint8_t count = 0;
    std::uint16_t used = 0;
    std::uint16_t dropped = 0;
    Row rows[kMaxRows] = {};
    std::uint8_t data[kArenaBytes] = {};
};
static_assert(std::is_trivially_copyable_v<CheatCatalog>);

using CheatCatalogCell = xthread::Telemetry<CheatCatalog, SeatTag::Unbound>;

}  // namespace mister::app
