// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "app/front_end_status.h"
#include "app/hd_epoch.h"
#include "app/hd_look.h"
#include "app/hd_osd_status.h"
#include "app/hd_status_slots.h"
#include "app/hd_surface.h"
#include "app/page_description.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/bit_field.h"
#include "proto/types.h"

namespace mister::app {

enum class FrontEndBg : std::uint8_t {
    None,
    Wallpaper,
    HBars1,
    HBars2,
    VBars1,
    VBars2,
    Spectrum,
    Black,
};

enum class FrontEndIdle : std::uint8_t { Awake, NoLogo, Dimmed, Dark, DarkLogo };

[[nodiscard]] constexpr FrontEndBg front_end_bg_of(proto::FieldValue value) noexcept {
    constexpr std::array<FrontEndBg, 8> kBgs{
        FrontEndBg::None,
        FrontEndBg::Wallpaper,
        FrontEndBg::HBars1,
        FrontEndBg::HBars2,
        FrontEndBg::VBars1,
        FrontEndBg::VBars2,
        FrontEndBg::Spectrum,
        FrontEndBg::Black
    };
    const std::size_t i = proto::printed_field(value);
    return i < kBgs.size() ? kBgs[i] : FrontEndBg::None;
}

[[nodiscard]] constexpr bool paints_pattern(FrontEndBg bg) noexcept {
    return bg != FrontEndBg::None;
}

enum class HdOsdShows : std::uint8_t { Menu, Background };

struct HdOsdAsk {
    bool open = false;
    HdOsdShows shows = HdOsdShows::Menu;
    HdEpoch epoch{};
    HdSurface surface{};
    std::uint32_t video_applies = 0;
    PageDescription page{};

    std::uint64_t stamp_ns = 0;

    HdOsdStatus::Badge badge = HdOsdStatus::Badge::Unknown;

    HdStatusSlots slots{};

    HdLook look{};

    FrontEndBg bg = FrontEndBg::None;

    FrontEndIdle idle = FrontEndIdle::Awake;

    FixedStr<8, StrFit::Clip> ini_label{};
};

static_assert(std::is_trivially_copyable_v<HdOsdAsk>);
static_assert(std::is_nothrow_default_constructible_v<HdOsdAsk>);

using HdOsdAskCell = xthread::Telemetry<HdOsdAsk, SeatTag::Ui>;

}
