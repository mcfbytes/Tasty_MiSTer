// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "infra/json_out.h"
#include "reactor/round_timing.h"
#include "record_ctx.h"

namespace mister::fw {

struct RndView {
    std::uint32_t epoch = 0;
    std::uint32_t steady_rounds = 0;
    std::uint32_t steady_avg_us = 0;
    std::uint32_t steady_hist[reactor::kRoundBuckets] = {};
    std::uint32_t all_steady_rounds = 0;
    std::uint32_t all_steady_avg_us = 0;
    std::uint32_t all_steady_hist[reactor::kRoundBuckets] = {};
};

constexpr void to_json(infra::JsonOut& o, const RndView& v) noexcept {
    o.field("ep", v.epoch);
    o.field("sn", v.steady_rounds);
    o.field("sa", v.steady_avg_us);
    o.begin_array("sh");
    for (const std::uint32_t c : v.steady_hist)
        o.elem(c);
    o.end_array();
    o.field("asn", v.all_steady_rounds);
    o.field("asa", v.all_steady_avg_us);
    o.begin_array("ash");
    for (const std::uint32_t c : v.all_steady_hist)
        o.elem(c);
    o.end_array();
}

struct RndRecord {
    static constexpr infra::JsonName kKey{"rndh"};
    using View = RndView;
    const reactor::RoundTimingCell& cell;

    [[nodiscard]] std::optional<View> sample(const RecordCtx&) const noexcept {
        const std::optional<reactor::RoundTiming> t = sample_published(cell);
        if (!t)
            return std::nullopt;
        View v{
            .epoch = t->epoch,
            .steady_rounds = t->steady.n,
            .steady_avg_us = reactor::round_avg_us(t->steady),
            .all_steady_rounds = t->all_steady_rounds,
            .all_steady_avg_us = reactor::round_all_steady_avg_us(*t)
        };
        for (std::size_t b = 0; b < reactor::kRoundBuckets; ++b) {
            v.steady_hist[b] = t->steady.hist[b];
            v.all_steady_hist[b] = t->all_steady_hist[b];
        }
        return v;
    }
};

}
