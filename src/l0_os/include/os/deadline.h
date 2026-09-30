// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "infra/error.h"
#include "os/clock.h"
#include "infra/seat.h"

namespace mister::os {

class Deadline {
    TASTY_SEAT_EXEMPT(component);

public:
    static Deadline in(const IClock& c, std::chrono::nanoseconds d) {
        return Deadline{c.now() + d};
    }
    static Deadline immediate() { return Deadline{std::chrono::nanoseconds{0}}; }

    bool expired(const IClock& c) const { return c.now() >= at_; }
    std::chrono::nanoseconds at() const noexcept { return at_; }

    Ex<void> sleep_until() const;

private:
    explicit Deadline(std::chrono::nanoseconds at) : at_(at) {}
    std::chrono::nanoseconds at_;
};

}  // namespace mister::os
