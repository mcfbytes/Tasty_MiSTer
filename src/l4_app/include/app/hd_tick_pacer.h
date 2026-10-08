// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::app {

class HdTickPacer {
    TASTY_SEAT_EXEMPT(component);

public:
    explicit constexpr HdTickPacer(std::uint8_t fps = 10) noexcept { set_fps(fps); }

    constexpr void set_fps(std::uint8_t fps) noexcept {
        fps_ = fps == 0 ? 1 : fps;
        period_ns_ = 1'000'000'000ull / fps_;
        next_ns_ = 0;
    }

    constexpr void resync() noexcept { next_ns_ = 0; }

    constexpr void reset() noexcept {
        next_ns_ = 0;
        missed_ = 0;
    }

    [[nodiscard]] constexpr bool due(std::uint64_t now) const noexcept {
        return next_ns_ == 0 || now >= next_ns_;
    }

    constexpr void advance(std::uint64_t now) noexcept {
        if (!due(now)) return;
        if (next_ns_ == 0) {
            next_ns_ = now + period_ns_;
            return;
        }
        const std::uint64_t late = now - next_ns_;
        const std::uint64_t skipped = late / period_ns_;
        missed_ += skipped;
        next_ns_ += (skipped + 1ull) * period_ns_;
    }

    [[nodiscard]] constexpr int park_ms(std::uint64_t now) const noexcept {
        if (due(now)) return 0;
        const std::uint64_t left = next_ns_ - now;
        std::uint64_t ms = (left + 999'999ull) / 1'000'000ull;
        if (ms > 1000ull) ms = 1000ull;
        return static_cast<int>(ms);
    }

    [[nodiscard]] constexpr std::uint64_t missed() const noexcept { return missed_; }
    [[nodiscard]] constexpr std::uint8_t fps() const noexcept { return fps_; }
    [[nodiscard]] constexpr std::uint64_t period_ns() const noexcept { return period_ns_; }

private:
    std::uint8_t fps_ = 10;
    std::uint64_t period_ns_ = 100'000'000ull;
    std::uint64_t next_ns_ = 0;
    std::uint64_t missed_ = 0;
};

}  // namespace mister::app
