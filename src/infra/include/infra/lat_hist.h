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
        sum_ += d;
        if (d < min_ || count_ == 1)
            min_ = d;
        if (d > max_)
            max_ = d;
    }

    std::uint64_t count() const noexcept { return count_; }

    std::chrono::nanoseconds max_ns() const noexcept { return max_; }

private:
    std::uint64_t count_ = 0;
    std::chrono::nanoseconds sum_{}, min_{}, max_{};
};

}
