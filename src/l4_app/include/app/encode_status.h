// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct EncodeStatus {

    struct Video {
        std::uint32_t encoded = 0;
        std::uint32_t dups = 0;
        std::uint32_t keys = 0;
        std::uint32_t chunk_full = 0;
        std::uint32_t enc_us_last = 0;
        std::uint32_t enc_us_max = 0;
        std::uint32_t budget_pct = 0;
        std::uint32_t me_cut = 0;
        std::uint32_t arena_kib = 0;
        std::uint16_t segment = 0;
        std::uint8_t scale = 0;
        std::uint8_t steps = 0;
        std::uint8_t fell_behind = 0;
        std::uint8_t recovered = 0;
        std::uint8_t held = 0;
        std::uint8_t queue_max = 0;
        std::uint8_t rate_rolls = 0;
        std::uint8_t me_rad = 0;
        std::uint32_t errors = 0;
        std::uint64_t bytes = 0;
    };

    std::uint32_t frames = 0;
    std::uint32_t rows = 0;
    std::uint32_t ring_full = 0;
    std::uint32_t hash_us_last = 0;
    std::uint32_t hash_us_max = 0;
    Video video{};
};

using EncodeStatusCell = xthread::Telemetry<EncodeStatus, SeatTag::Encode>;

}  // namespace mister::app
