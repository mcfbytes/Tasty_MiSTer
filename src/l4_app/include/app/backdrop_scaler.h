// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "app/hd_rect.h"
#include "app/hd_theme.h"
#include "infra/seat.h"

namespace mister::app {

class BackdropScaler {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    void open(const HdTheme& theme) noexcept;

    [[nodiscard]] HdRect scale(std::span<const std::byte> rgb, std::uint16_t src_w,
                               std::uint16_t src_h, std::uint16_t src_line,
                               std::span<std::uint16_t> frame, std::uint32_t stride_px, HdRect game,
                               HdRect cover, bool report) noexcept;

private:
    void ensure_xmap_(std::uint16_t src_w, std::int32_t cover_w) noexcept;

    std::uint16_t primary_ = 0;
    std::uint16_t q8_ = 102;
    std::uint16_t src_w_ = 0;
    std::vector<std::uint16_t> xmap_{};
};

}  // namespace mister::app
