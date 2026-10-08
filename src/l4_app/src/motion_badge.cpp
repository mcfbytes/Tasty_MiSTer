// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/motion_badge.h"

#include <cstdint>

namespace mister::app {
namespace {

[[nodiscard]] std::uint8_t luma(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    return static_cast<std::uint8_t>((static_cast<unsigned>(r) * 77u +
                                      static_cast<unsigned>(g) * 150u +
                                      static_cast<unsigned>(b) * 29u) >>
                                     8);
}

}  // namespace

void MotionBadge::reset() noexcept {
    TASTY_SEAT_BODY(MotionBadge);
    have_ = false;
    n_ = 0;
    head_ = 0;
}

HdOsdStatus::Badge MotionBadge::observe(std::span<const std::byte> rgb, std::uint16_t width,
                                        std::uint16_t height, std::uint16_t line) noexcept {
    TASTY_SEAT_BODY(MotionBadge);
    std::array<std::uint8_t, kCells> grid{};
    for (int cy = 0; cy < kGridH; ++cy) {
        for (int cx = 0; cx < kGridW; ++cx) {
            std::uint8_t y8 = 0;
            if (width != 0 && height != 0 && line != 0) {
                const auto x0 =
                    static_cast<std::uint32_t>(cx) * width / static_cast<std::uint32_t>(kGridW);
                const auto x1 =
                    static_cast<std::uint32_t>(cx + 1) * width / static_cast<std::uint32_t>(kGridW);
                const auto y0 =
                    static_cast<std::uint32_t>(cy) * height / static_cast<std::uint32_t>(kGridH);
                const auto y1 = static_cast<std::uint32_t>(cy + 1) * height /
                                static_cast<std::uint32_t>(kGridH);
                const auto x = x0 + (x1 - x0) / 2u;
                const auto y = y0 + (y1 - y0) / 2u;
                const std::size_t i =
                    static_cast<std::size_t>(y) * line + static_cast<std::size_t>(x) * 3u;
                if (i + 2 < rgb.size())
                    y8 = luma(static_cast<std::uint8_t>(rgb[i]),
                              static_cast<std::uint8_t>(rgb[i + 1]),
                              static_cast<std::uint8_t>(rgb[i + 2]));
            }
            grid[static_cast<std::size_t>(cy * kGridW + cx)] = y8;
        }
    }
    if (!have_) {
        prev_ = grid;
        have_ = true;
        return HdOsdStatus::Badge::Unknown;
    }
    std::uint32_t acc = 0;
    for (std::size_t i = 0; i < grid.size(); ++i) {
        const int d = static_cast<int>(grid[i]) - static_cast<int>(prev_[i]);
        acc += static_cast<std::uint32_t>(d < 0 ? -d : d);
    }
    prev_ = grid;
    const std::uint32_t mean = acc / static_cast<std::uint32_t>(kCells);
    if (n_ < deltas_.size()) {
        deltas_[n_++] = mean;
    } else {
        deltas_[head_] = mean;
        head_ = static_cast<std::uint8_t>((head_ + 1u) % deltas_.size());
    }
    bool moving = false;
    for (std::uint8_t i = 0; i < n_; ++i)
        if (deltas_[i] > kStillDelta) moving = true;
    if (moving) return HdOsdStatus::Badge::Running;
    if (n_ == deltas_.size()) return HdOsdStatus::Badge::Paused;
    return HdOsdStatus::Badge::Unknown;
}

}  // namespace mister::app
