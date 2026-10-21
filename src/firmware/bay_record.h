// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/bay_counts.h"
#include "infra/json_out.h"
#include "record_ctx.h"

namespace mister::fw {

struct BayView {
    std::uint32_t builds = 0;
    std::uint32_t bay_full = 0;
    std::uint32_t frees = 0;
    std::uint32_t window_refusals = 0;
    std::uint32_t fed_refusals = 0;
};

constexpr void to_json(infra::JsonOut& o, const BayView& v) noexcept {
    o.field("build", v.builds);
    o.field("full", v.bay_full);
    o.field("free", v.frees);
    o.field("wref", v.window_refusals);
    o.field("fedref", v.fed_refusals);
}

struct BayRecord {
    static constexpr infra::JsonName kKey{"bay"};
    using View = BayView;
    const app::BayCountsCell& counts;

    [[nodiscard]] std::optional<View> sample(const RecordCtx& ctx) const noexcept {
        const app::BayCounts c = counts.sample().value;
        return View{
            .builds = c.builds,
            .bay_full = c.bay_full,
            .frees = c.frees,
            .window_refusals = c.window_refusals,
            .fed_refusals = ctx.dc.fed_refusals
        };
    }
};

}
