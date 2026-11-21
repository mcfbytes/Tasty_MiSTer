// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace mister::infra {

template <class Instant>
class PeriodGrid {
public:
    struct Periods {
        std::uint64_t v;
        using affine_count = Periods;

        [[nodiscard]] constexpr std::uint64_t missed() const noexcept { return v - 1; }

        [[nodiscard]] constexpr std::uint64_t ticks() const noexcept { return v; }
    };

    struct Moment {
        Instant next;
        Instant now;
    };

    struct Advance {
        Instant latest;
        Periods periods;
        Instant next;
    };

    explicit constexpr PeriodGrid(std::chrono::nanoseconds period) noexcept : period_(period) {}

    [[nodiscard]] constexpr std::chrono::nanoseconds period() const noexcept { return period_; }

    [[nodiscard]] constexpr bool due_at(Moment m) const noexcept { return m.now >= m.next; }

    [[nodiscard]] constexpr std::optional<Advance> advance(Moment m) const noexcept {
        const Instant next = m.next;
        const Instant now = m.now;
        if (now < next)
            return std::nullopt;
        if (now < next + period_) {
            return Advance{next, Periods{1}, next + period_};
        }

        const std::chrono::nanoseconds late = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now - next
        );
        const std::int64_t missed = late / period_;
        const Instant latest = next + period_ * missed;
        return Advance{latest, Periods{static_cast<std::uint64_t>(missed) + 1}, latest + period_};
    }

    [[nodiscard]] constexpr std::chrono::nanoseconds remaining(Moment m) const noexcept {
        const std::chrono::nanoseconds left = std::chrono::duration_cast<std::chrono::nanoseconds>(
            m.next - m.now
        );
        return left < std::chrono::nanoseconds::zero() ? std::chrono::nanoseconds::zero() : left;
    }

private:
    std::chrono::nanoseconds period_;
};

}
