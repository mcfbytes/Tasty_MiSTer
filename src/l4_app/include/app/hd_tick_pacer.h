// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "infra/park_timeout.h"
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
        if (!due(now))
            return;
        if (next_ns_ == 0) {
            next_ns_ = now + period_ns_;
            return;
        }
        const std::uint64_t late = now - next_ns_;
        const std::uint64_t skipped = late / period_ns_;
        missed_ += skipped;
        next_ns_ += (skipped + 1ull) * period_ns_;
    }

    [[nodiscard]] constexpr infra::ParkTimeout park_ms(std::uint64_t now) const noexcept {
        if (due(now))
            return infra::kNoWait;
        constexpr infra::ParkTimeout kCeil{1000};
        using Ms = std::chrono::duration<std::uint64_t, std::milli>;
        const Ms ms = std::chrono::ceil<Ms>(
            std::chrono::duration<std::uint64_t, std::nano>{next_ns_ - now}
        );
        return ms > kCeil ? kCeil : std::chrono::duration_cast<infra::ParkTimeout>(ms);
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

}
