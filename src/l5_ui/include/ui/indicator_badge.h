// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <tuple>

#include "proto/osd_surface.h"
#include "proto/types.h"
#include "ui/indicator_canvas.h"

namespace mister::ui {

using IconArt = std::array<std::string_view, 16>;

[[nodiscard]] const IconArt& icon_art(IndicatorIcon icon) noexcept;

class IndicatorBadge {
public:
    using RowBytes = std::array<std::uint8_t, proto::OsdSurface::kRowBytes>;

    using Lines = std::array<IndicatorCanvas, 3>;

    explicit IndicatorBadge(std::span<const IndicatorCanvas> lines) noexcept;

    [[nodiscard]] proto::OsdColCount width() const noexcept { return width_; }

    [[nodiscard]] proto::OsdRowCount height() const noexcept { return height_; }

    [[nodiscard]] RowBytes row(proto::OsdRow row) const noexcept;

private:
    proto::OsdRowCells<RowBytes, 2 * std::tuple_size_v<Lines> + 1> rows_{};
    proto::OsdColCount width_{};
    proto::OsdRowCount height_{};
};

}
