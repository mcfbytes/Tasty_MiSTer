// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "infra/error.h"
#include "infra/niche.h"
#include "infra/prelude.h"
#include "os/clock.h"
#include "infra/seat.h"

namespace mister::os {

class Deadline {

public:
    struct NichePolicy {
        [[nodiscard]] static constexpr Deadline none() noexcept { return Deadline::immediate(); }

        [[nodiscard]] static constexpr bool is_none(const Deadline& d) noexcept {
            return d.at() == RawMonotonicClock::time_point{};
        }
    };

    static Deadline in(const IClock& c, std::chrono::nanoseconds d) {
        return Deadline{c.now() + d};
    }

    static constexpr Deadline immediate() noexcept {
        return Deadline{RawMonotonicClock::time_point{}};
    }

    [[nodiscard]] static constexpr Deadline at_point(RawMonotonicClock::time_point at) noexcept {
        return Deadline{at};
    }

    [[nodiscard]] bool expired(const IClock& c) const { return expired_at(c.now()); }

    [[nodiscard]] constexpr bool expired_at(RawMonotonicClock::time_point now) const noexcept {
        return now >= at_;
    }

    [[nodiscard]] constexpr RawMonotonicClock::time_point at() const noexcept { return at_; }

    [[nodiscard]] constexpr std::chrono::nanoseconds remaining(
        RawMonotonicClock::time_point now
    ) const noexcept {
        return at_ - now;
    }

    [[nodiscard]] Ex<void> sleep_until() const;

private:
    explicit constexpr Deadline(RawMonotonicClock::time_point at) noexcept : at_(at) {}

    RawMonotonicClock::time_point at_;
};

}
