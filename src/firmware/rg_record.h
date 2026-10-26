// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/ui_page_record.h"
#include "infra/json_out.h"
#include "infra/opt_ref.h"
#include "record_ctx.h"

namespace mister::fw {

struct RgView {
    std::uint32_t build_us = 0;
    std::uint32_t update_us_max = 0;
};

constexpr void to_json(infra::JsonOut& o, const RgView& v) noexcept {
    o.field("build_us", v.build_us);
    o.field("update_us_max", v.update_us_max);
}

struct RgRecord {
    static constexpr infra::JsonName kKey{"rg"};
    using View = RgView;
    infra::OptRef<const app::UiPageCell> cell{};

    [[nodiscard]] std::optional<View> sample(const RecordCtx&) const noexcept {
        const auto page = sample_published(cell);
        if (!page)
            return std::nullopt;
        return View{page->recent_build_us, page->recent_update_us_max};
    }
};

}
