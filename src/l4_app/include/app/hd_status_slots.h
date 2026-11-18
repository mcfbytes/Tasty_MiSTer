// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "app/indicator_id.h"
#include "infra/ascii.h"
#include "infra/message_sum.h"

namespace mister::app {

struct HdStatusSlots {
    bool bluetooth = false;
    bool rt = false;

    IndicatorSet indicators{};
    friend bool operator==(const HdStatusSlots&, const HdStatusSlots&) = default;
};

static_assert(std::is_trivially_copyable_v<HdStatusSlots>);
static_assert(std::is_nothrow_default_constructible_v<HdStatusSlots>);

struct HdIndicatorTag {
    IndicatorId id;
    std::string_view tag;
};

inline constexpr PerIndicator<HdIndicatorTag> kHdIndicatorTags{{
    {IndicatorId::Saved, "SAVED"},
    {IndicatorId::SaveFailed, "SAVE FAILED"},
}};

consteval bool hd_indicator_tags_follow_ids() {
    for (std::size_t i = 0; i < kHdIndicatorTags.size(); ++i) {
        if (infra::ordinal(kHdIndicatorTags[i].id) != i)
            return false;
    }
    return true;
}

static_assert(hd_indicator_tags_follow_ids(), "kHdIndicatorTags holds one tag per IndicatorId");

inline constexpr std::string_view kHdBluetoothTag = "BT";
inline constexpr std::string_view kHdRtTag = "RT";
inline constexpr std::string_view kHdStatusTagGap = "  ";

inline constexpr infra::ByteCount kHdStatusLineWidth{26};

consteval bool hd_status_line_fits() {
    std::size_t width = kHdBluetoothTag.size() + kHdRtTag.size();
    for (const HdIndicatorTag& row : kHdIndicatorTags)
        width += row.tag.size() + kHdStatusTagGap.size();
    return infra::ByteCount{width + kHdStatusTagGap.size()} == kHdStatusLineWidth;
}

static_assert(hd_status_line_fits(), "kHdStatusLineWidth spells BT, RT and every tag, a gap apart");

}
