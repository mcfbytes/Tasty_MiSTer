// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "app/indicator_id.h"
#include "app/info_plane.h"
#include "infra/message_sum.h"
#include "proto/osd_target.h"
#include "ui/indicator_canvas.h"

namespace mister::ui {

enum class IndicatorKind : std::uint8_t { Toast, Overlay };

struct IndicatorRow {
    app::IndicatorId id;
    IndicatorKind kind;
    proto::OsdTarget targets;
    std::string_view text;
    IndicatorIcon icon = IndicatorIcon::None;
    app::InfoAnchor anchor = app::InfoAnchor::InfoOrigin;
};

inline constexpr app::PerIndicator<IndicatorRow> kIndicatorRows{{
    {app::IndicatorId::Saved,
     IndicatorKind::Toast,
     proto::OsdTarget::All,
     "Saved",
     IndicatorIcon::Card,
     app::InfoAnchor::BottomRight},
    {app::IndicatorId::SaveFailed,
     IndicatorKind::Toast,
     proto::OsdTarget::All,
     "Save failed",
     IndicatorIcon::CardFault,
     app::InfoAnchor::BottomRight},
}};

consteval bool indicator_rows_follow_ids() {
    for (std::size_t i = 0; i < kIndicatorRows.size(); ++i) {
        if (infra::ordinal(kIndicatorRows[i].id) != i)
            return false;
    }
    return true;
}

static_assert(
    indicator_rows_follow_ids(),
    "kIndicatorRows holds one row per IndicatorId, in order"
);

[[nodiscard]] constexpr const IndicatorRow& indicator_row(app::IndicatorId id) noexcept {
    return kIndicatorRows[infra::ordinal(id)];
}

}
