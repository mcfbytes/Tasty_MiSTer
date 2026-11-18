// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>

#include "os/clock.h"

namespace mister::testing {

class StepClock final : public os::IClock {
public:
    StepClock() = default;

    explicit StepClock(std::chrono::nanoseconds start) : at_(start) {}

    StepClock(std::chrono::nanoseconds start, std::chrono::nanoseconds per_read)
        : at_(start),
          per_read_(per_read) {}

    os::RawMonotonicClock::time_point now() const override {
        ++reads_;
        const std::chrono::nanoseconds at = at_;
        at_ += per_read_;
        return os::RawMonotonicClock::time_point{at};
    }

    void set(std::chrono::nanoseconds at) { at_ = at; }

    void advance(std::chrono::nanoseconds by) { at_ += by; }

    void advance_ms(int ms) { advance(std::chrono::milliseconds{ms}); }

    void advance_s(int s) { advance(std::chrono::seconds{s}); }

    [[nodiscard]] std::chrono::nanoseconds since_epoch() const { return at_; }

    [[nodiscard]] std::size_t reads() const { return reads_; }

private:
    static constexpr std::chrono::nanoseconds kStart{1};

    mutable std::chrono::nanoseconds at_ = kStart;
    std::chrono::nanoseconds per_read_{};
    mutable std::size_t reads_ = 0;
};

}
