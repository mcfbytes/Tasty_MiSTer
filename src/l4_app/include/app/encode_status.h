// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/lat_stat.h"
#include "app/percent.h"
#include "app/rec_options.h"
#include "app/scale_governor.h"
#include "app/step_count.h"
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
        LatStat enc_us{};
        Percent budget_pct{};
        std::uint32_t me_cut = 0;
        std::uint32_t arena_kib = 0;
        std::uint16_t segment = 0;

        std::optional<PictureScale> scale{};
        StepCount steps{};
        bool fell_behind = false;
        StepCount recovered{};
        bool held = false;
        std::uint8_t queue_max = 0;
        std::uint8_t rate_rolls = 0;
        std::uint8_t me_rad = 0;
        std::uint32_t errors = 0;
        std::uint64_t bytes = 0;
    };

    std::uint32_t frames = 0;
    std::uint32_t rows = 0;
    std::uint32_t ring_full = 0;
    LatStat hash_us{};
    Video video{};
};

constexpr void to_json(infra::JsonOut& o, const EncodeStatus& e) noexcept {
    o.field("hashed", e.frames);
    o.field("hash_us", e.hash_us.last.count());
    o.field("hash_us_max", e.hash_us.max.count());
    o.field("enc_held", e.ring_full);
}

constexpr void to_json(infra::JsonOut& o, const EncodeStatus::Video& v) noexcept {
    o.field("scale", picture_scale_digit(v.scale));
    o.field("steps", v.steps);
    o.field("recovered", v.recovered);
    o.field("held", v.held);
    o.field("q_max", v.queue_max);
    o.field("encoded", v.encoded);
    o.field("dups", v.dups);
    o.field("keys", v.keys);
    o.field("enc_us", v.enc_us.last.count());
    o.field("enc_us_max", v.enc_us.max.count());
    o.field("budget_pct", v.budget_pct);
    o.field("me_rad", v.me_rad);
    o.field("me_cut", v.me_cut);
    o.field("chunk_full", v.chunk_full);
    o.field("enc_err", v.errors);
    o.field("chunk_kib", v.arena_kib);
}

using EncodeStatusCell = xthread::Telemetry<EncodeStatus, SeatTag::Encode>;

}
