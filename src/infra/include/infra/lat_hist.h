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
        const auto ns = static_cast<std::uint64_t>(d.count());
        ++count_;
        sum_ns_ += ns;
        if (ns < min_ns_ || count_ == 1) min_ns_ = ns;
        if (ns > max_ns_) max_ns_ = ns;
    }
    std::uint64_t count() const noexcept { return count_; }
    std::uint64_t max_ns() const noexcept { return max_ns_; }

private:
    std::uint64_t count_ = 0, sum_ns_ = 0, min_ns_ = 0, max_ns_ = 0;
};

}  // namespace mister
