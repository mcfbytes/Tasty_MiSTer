// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <optional>

#include "app/output_pixels.h"
#include "app/output_size.h"
#include "infra/affine.h"
#include "proto/info_window.h"
#include "proto/osd_rotation.h"
#include "proto/types.h"

namespace mister::app {

enum class InfoAnchor : std::uint8_t { InfoOrigin, BottomRight };

struct InfoSpan {
    std::uint32_t v = 0;
    using affine_count = InfoSpan;

    [[nodiscard]] static constexpr InfoSpan across(OutputPixels clocks) noexcept {
        constexpr std::uint32_t kPixelShift = 9;
        const std::uint32_t pixel = clocks.v >> kPixelShift;
        return InfoSpan{pixel > 1 ? clocks.v / pixel : clocks.v};
    }

    [[nodiscard]] static constexpr InfoSpan down(OutputPixels lines) noexcept {
        constexpr std::array<std::uint32_t, 4> kBandEdges{320, 640, 960, 1280};
        std::uint32_t band = 1;
        for (const std::uint32_t edge : kBandEdges) {
            if (lines.v >= edge)
                ++band;
        }
        return InfoSpan{lines.v / band};
    }

    [[nodiscard]] static constexpr InfoSpan of(proto::OsdColCount cells) noexcept {
        constexpr std::uint32_t kCellPixels = 8;
        return InfoSpan{cells.v * kCellPixels};
    }

    [[nodiscard]] static constexpr InfoSpan of(proto::OsdRowCount rows) noexcept {
        constexpr std::uint32_t kRowPixels = 8;
        return InfoSpan{rows.v * kRowPixels};
    }

    [[nodiscard]] constexpr proto::OsdCol as_x() const noexcept { return proto::OsdCol{v}; }

    [[nodiscard]] constexpr proto::OsdRow as_y() const noexcept { return proto::OsdRow{v}; }

    friend constexpr bool operator==(InfoSpan, InfoSpan) = default;
    friend constexpr auto operator<=>(InfoSpan, InfoSpan) = default;
};

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;

struct InfoPlane {
    InfoSpan across{};
    InfoSpan down{};

    static constexpr InfoSpan kCornerInset{12};

    static constexpr InfoSpan kAnalogAcrossFloor{512};

    [[nodiscard]] static constexpr InfoPlane of(OutputSize size) noexcept {
        return InfoPlane{InfoSpan::across(size.w), InfoSpan::down(size.h)};
    }

    [[nodiscard]] static constexpr InfoPlane analog(OutputPixels lines) noexcept {
        return InfoPlane{kAnalogAcrossFloor, InfoSpan::down(lines)};
    }

    [[nodiscard]] constexpr InfoPlane meet(InfoPlane other) const noexcept {
        return InfoPlane{
            across < other.across ? across : other.across,
            down < other.down ? down : other.down
        };
    }

    [[nodiscard]] constexpr std::optional<proto::InfoWindow> bottom_right(
        proto::InfoWindow w
    ) const noexcept {
        const InfoSpan wide = InfoSpan::of(w.width) + kCornerInset;
        const InfoSpan tall = InfoSpan::of(w.height) + kCornerInset;
        if (across < wide || down < tall)
            return std::nullopt;
        w.x = (across - wide).as_x();
        w.y = (down - tall).as_y();
        return w;
    }

    friend constexpr bool operator==(InfoPlane, InfoPlane) = default;
};

struct InfoPlaneFacts {
    proto::OsdRotation rotation = proto::OsdRotation::None;
    bool direct_video = false;
    bool hd_style = false;
    OutputSize output{};
    std::optional<OutputPixels> core_lines{};
};

[[nodiscard]] constexpr std::optional<InfoPlane> info_plane_for(const InfoPlaneFacts& f) noexcept {
    if (f.rotation != proto::OsdRotation::None)
        return std::nullopt;
    const std::optional<InfoPlane> analog = f.core_lines
        ? std::optional{InfoPlane::analog(*f.core_lines)}
        : std::nullopt;
    if (f.direct_video)
        return analog;
    if (f.output.w.empty() || f.output.h.empty())
        return std::nullopt;
    const InfoPlane hdmi = InfoPlane::of(f.output);
    if (f.hd_style)
        return hdmi;
    return analog ? std::optional{hdmi.meet(*analog)} : std::nullopt;
}

[[nodiscard]] constexpr proto::InfoWindow placed(
    proto::InfoWindow w,
    InfoAnchor anchor,
    proto::OsdRow origin,
    std::optional<InfoPlane> plane
) noexcept {
    w.y = origin;
    switch (anchor) {
        case InfoAnchor::InfoOrigin:
            return w;
        case InfoAnchor::BottomRight:
            return plane ? plane->bottom_right(w).value_or(w) : w;
    }
    return w;
}

}
