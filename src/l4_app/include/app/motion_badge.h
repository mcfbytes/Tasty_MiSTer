// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "app/hd_osd_status.h"
#include "infra/seat.h"

namespace mister::app {

class MotionBadge {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    static constexpr int kGridW = 16;
    static constexpr int kGridH = 12;
    static constexpr int kCells = kGridW * kGridH;
    static constexpr std::uint32_t kStillDelta = 8;

    void reset() noexcept;
    [[nodiscard]] HdOsdStatus::Badge observe(std::span<const std::byte> rgb, std::uint16_t width,
                                             std::uint16_t height, std::uint16_t line) noexcept;

private:
    std::array<std::uint8_t, kCells> prev_{};
    std::array<std::uint32_t, 3> deltas_{};
    bool have_ = false;
    std::uint8_t n_ = 0;
    std::uint8_t head_ = 0;
};

}  // namespace mister::app
