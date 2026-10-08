// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/json_out.h"
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

constexpr void to_json(infra::JsonOut& o, const EncodeStatus& e) noexcept {
    o.field("hashed", e.frames);
    o.field("hash_us", e.hash_us_last);
    o.field("hash_us_max", e.hash_us_max);
    o.field("enc_held", e.ring_full);
}

constexpr void to_json(infra::JsonOut& o, const EncodeStatus::Video& v) noexcept {
    o.field("scale", v.scale);
    o.field("steps", v.steps);
    o.field("recovered", v.recovered);
    o.field("held", v.held);
    o.field("q_max", v.queue_max);
    o.field("encoded", v.encoded);
    o.field("dups", v.dups);
    o.field("keys", v.keys);
    o.field("enc_us", v.enc_us_last);
    o.field("enc_us_max", v.enc_us_max);
    o.field("budget_pct", v.budget_pct);
    o.field("me_rad", v.me_rad);
    o.field("me_cut", v.me_cut);
    o.field("chunk_full", v.chunk_full);
    o.field("enc_err", v.errors);
    o.field("chunk_kib", v.arena_kib);
}

using EncodeStatusCell = xthread::Telemetry<EncodeStatus, SeatTag::Encode>;

}  // namespace mister::app
