// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "infra/park_timeout.h"
#include "infra/period_grid.h"
#include "infra/seat.h"
#include "os/raw_monotonic_clock.h"

namespace mister::app::hd {

class HdTickPacer {
    TASTY_SEAT_EXEMPT(component);
    using Instant = os::RawMonotonicClock::time_point;

public:
    explicit constexpr HdTickPacer(std::uint8_t fps = 10) noexcept { set_fps(fps); }

    constexpr void set_fps(std::uint8_t fps) noexcept {
        fps_ = fps == 0 ? 1 : fps;
        period_ns_ = 1'000'000'000ull / fps_;
        grid_ = infra::PeriodGrid<Instant>{period()};
        next_ = std::nullopt;
    }

    constexpr void resync() noexcept { next_ = std::nullopt; }

    constexpr void reset() noexcept {
        next_ = std::nullopt;
        missed_ = 0;
    }

    [[nodiscard]] constexpr bool due(Instant now) const noexcept {
        return !next_ || grid_.due_at({.next = *next_, .now = now});
    }

    constexpr void advance(Instant now) noexcept {
        if (!next_) {
            next_ = now + period();
            return;
        }
        if (const auto step = grid_.advance({.next = *next_, .now = now})) {
            next_ = step->next;
            missed_ += step->periods.missed();
        }
    }

    [[nodiscard]] constexpr infra::ParkTimeout park_ms(Instant now) const noexcept {
        if (!next_)
            return infra::kNoWait;
        constexpr infra::ParkTimeout kCeil{1000};
        return infra::park_timeout_for(grid_.remaining({.next = *next_, .now = now}), kCeil);
    }

    [[nodiscard]] constexpr std::uint64_t missed() const noexcept { return missed_; }

    [[nodiscard]] constexpr std::uint8_t fps() const noexcept { return fps_; }

    [[nodiscard]] constexpr std::uint64_t period_ns() const noexcept { return period_ns_; }

private:
    using UnsignedNs = std::chrono::duration<std::uint64_t, std::nano>;

    [[nodiscard]] constexpr std::chrono::nanoseconds period() const noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(UnsignedNs{period_ns_});
    }

    std::uint8_t fps_ = 10;
    std::uint64_t period_ns_ = 100'000'000ull;
    infra::PeriodGrid<Instant> grid_{period()};
    std::optional<Instant> next_{};
    std::uint64_t missed_ = 0;
};

}
