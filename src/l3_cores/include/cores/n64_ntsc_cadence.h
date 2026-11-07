// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "infra/seat.h"

namespace mister::cores::n64 {

class NtscCadence {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::chrono::milliseconds kPalGap{20};
    static constexpr std::chrono::milliseconds kShortGap{16};
    static constexpr std::chrono::milliseconds kLongGap{17};

    [[nodiscard]] std::chrono::milliseconds next_gap(bool pal) noexcept;

private:
    enum class Step : std::uint8_t { Unprimed, Wrapped, FirstLong, SecondLong, Short };
    Step step_ = Step::Unprimed;
};

}
