// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "app/lat_stat.h"
#include "app/log_clock.h"
#include "app/picture_row.h"
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
    LatStat render_us{};
    LatStat copy_us{};
    LatStat sample_us{};
    LatStat read_us{};
    LatStat scale_us{};
    std::uint16_t backdrop_w = 0;
    PictureRows backdrop_h{};
    std::uint16_t stride_mib = 0;
    std::uint8_t lowlat = 0;

    std::uint8_t uio = 0;

    std::uint32_t bd_same = 0;
    LatStat panel_us{};

    StatusMicros ask_us_min{};
    StatusMicros ask_us_p50{};
    StatusMicros ask_us_max{};
    std::uint32_t ask_n = 0;
    std::uint32_t inplace = 0;
    std::uint32_t flips = 0;
    std::uint32_t bd_frames = 0;
    LatStat bd_us{};
    std::uint32_t copy_kb = 0;
};

static_assert(std::is_trivially_copyable_v<HdOsdStatus>);
static_assert(std::is_nothrow_default_constructible_v<HdOsdStatus>);

constexpr void to_json(infra::JsonOut& o, const HdOsdStatus& s) noexcept {
    o.field("state", s.state);
    o.field("fail", s.fail);
    o.field("badge", s.badge);
    o.field("fps", s.fps);
    o.field("skipped", s.skipped);
    o.field("render_us", s.render_us.last.count());
    o.field("render_us_max", s.render_us.max.count());
    o.field("copy_us", s.copy_us.last.count());
    o.field("copy_us_max", s.copy_us.max.count());
    o.field("sample_us", s.sample_us.last.count());
    o.field("sample_us_max", s.sample_us.max.count());
    o.field("read_us", s.read_us.last.count());
    o.field("read_us_max", s.read_us.max.count());
    o.field("scale_us", s.scale_us.last.count());
    o.field("scale_us_max", s.scale_us.max.count());
    o.field("bw", s.backdrop_w);
    o.field("bh", s.backdrop_h);
    o.field("stride_mib", s.stride_mib);
    o.field("lowlat", s.lowlat);
    o.field("uio", s.uio);
    o.field("bd_same", s.bd_same);
    o.field("panel_us", s.panel_us.last.count());
    o.field("panel_us_max", s.panel_us.max.count());
    o.field("ask_us_min", s.ask_us_min.count());
    o.field("ask_us", s.ask_us_p50.count());
    o.field("ask_us_max", s.ask_us_max.count());
    o.field("ask_n", s.ask_n);
    o.field("inplace", s.inplace);
    o.field("flips", s.flips);
    o.field("bd_frames", s.bd_frames);
    o.field("bd_us", s.bd_us.last.count());
    o.field("bd_us_max", s.bd_us.max.count());
    o.field("copy_kb", s.copy_kb);
}

using HdOsdStatusCell = xthread::Telemetry<HdOsdStatus, SeatTag::HdOsd>;

}
