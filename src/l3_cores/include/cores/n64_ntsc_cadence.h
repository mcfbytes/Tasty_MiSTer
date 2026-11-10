// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "cores/n64_db_tags.h"
#include "infra/seat.h"

namespace mister::cores::n64 {

class NtscCadence {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::chrono::milliseconds kPalGap{20};
    static constexpr std::chrono::milliseconds kShortGap{16};
    static constexpr std::chrono::milliseconds kLongGap{17};

    [[nodiscard]] std::chrono::milliseconds next_gap(SystemType system) noexcept {
        std::chrono::milliseconds gap = kLongGap;
        switch (step_) {
            case Step::Unprimed:
                gap = kShortGap;
                step_ = Step::Wrapped;
                break;
            case Step::Wrapped:
                step_ = Step::FirstLong;
                break;
            case Step::FirstLong:
                step_ = Step::SecondLong;
                break;
            case Step::SecondLong:
                step_ = Step::Short;
                break;
            case Step::Short:
                gap = kShortGap;
                step_ = Step::FirstLong;
                break;
        }
        return system == SystemType::Pal ? kPalGap : gap;
    }

private:
    enum class Step : std::uint8_t { Unprimed, Wrapped, FirstLong, SecondLong, Short };
    Step step_ = Step::Unprimed;
};

}
