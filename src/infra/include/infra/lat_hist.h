// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "infra/seat.h"

namespace mister {

class LatHist {
    TASTY_SEAT_EXEMPT(component);

public:
    void record(std::chrono::nanoseconds d) noexcept {
        ++count_;
        if (d > max_ || count_ == 1)
            max_ = d;
    }

    std::uint64_t count() const noexcept { return count_; }

    std::chrono::nanoseconds max_ns() const noexcept { return max_; }

private:
    std::uint64_t count_ = 0;
    std::chrono::nanoseconds max_{};
};

}
