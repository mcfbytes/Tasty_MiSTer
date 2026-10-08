// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "app/hd_surface.h"
#include "app/page_description.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct HdOsdAsk {
    bool open = false;
    std::uint32_t epoch = 0;
    HdSurface surface{};
    std::uint32_t video_applies = 0;
    PageDescription page{};
};

static_assert(std::is_trivially_copyable_v<HdOsdAsk>);
static_assert(std::is_nothrow_default_constructible_v<HdOsdAsk>);

using HdOsdAskCell = xthread::Telemetry<HdOsdAsk, SeatTag::Ui>;

}  // namespace mister::app
