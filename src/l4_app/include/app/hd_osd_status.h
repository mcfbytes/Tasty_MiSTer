// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/json_out.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct HdOsdStatus {
    enum class State : std::uint8_t { Off, Ready, Showing, Failed };
    enum class Fail : std::uint8_t { None, NoSlotWindow, RasterInit, PortStuck, Deadline };
    enum class Badge : std::uint8_t { Unknown, Running, Paused };

    State state = State::Off;
    Fail fail = Fail::None;
    Badge badge = Badge::Unknown;
    std::uint8_t fps = 0;
    std::uint32_t skipped = 0;
    std::uint32_t render_us_last = 0;
    std::uint32_t render_us_max = 0;
    std::uint32_t copy_us_last = 0;
    std::uint32_t copy_us_max = 0;
    std::uint32_t sample_us_last = 0;
    std::uint32_t sample_us_max = 0;
    std::uint16_t backdrop_w = 0;
    std::uint16_t backdrop_h = 0;
    std::uint16_t stride_mib = 0;
    std::uint8_t lowlat = 0;
};

static_assert(std::is_trivially_copyable_v<HdOsdStatus>);
static_assert(std::is_nothrow_default_constructible_v<HdOsdStatus>);

constexpr void to_json(infra::JsonOut& o, const HdOsdStatus& s) noexcept {
    o.field("state", s.state);
    o.field("fail", s.fail);
    o.field("badge", s.badge);
    o.field("fps", s.fps);
    o.field("skipped", s.skipped);
    o.field("render_us", s.render_us_last);
    o.field("render_us_max", s.render_us_max);
    o.field("copy_us", s.copy_us_last);
    o.field("copy_us_max", s.copy_us_max);
    o.field("sample_us", s.sample_us_last);
    o.field("sample_us_max", s.sample_us_max);
    o.field("bw", s.backdrop_w);
    o.field("bh", s.backdrop_h);
    o.field("stride_mib", s.stride_mib);
    o.field("lowlat", s.lowlat);
}

using HdOsdStatusCell = xthread::Telemetry<HdOsdStatus, SeatTag::HdOsd>;

}  // namespace mister::app
