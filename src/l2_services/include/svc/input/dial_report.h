// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "proto/paddle_pos.h"
#include "proto/spinner_travel.h"

namespace mister::svc::input {

struct DialReport {
    std::optional<proto::PaddlePos> paddle{};
    proto::SpinnerTravel travel{};
    bool range_fault = false;

    [[nodiscard]] constexpr DialReport with_paddle(proto::PaddlePos sample) const noexcept {
        DialReport out = *this;
        out.paddle = sample;
        return out;
    }

    [[nodiscard]] constexpr DialReport with_range_fault() const noexcept {
        DialReport out = *this;
        out.range_fault = true;
        return out;
    }

    [[nodiscard]] constexpr DialReport with_spin(proto::SpinnerTravel step) const noexcept {
        DialReport out = *this;
        out.travel = travel.plus(step);
        return out;
    }

    [[nodiscard]] constexpr DialReport folded(const DialReport& later) const noexcept {
        DialReport out = *this;
        if (later.paddle)
            out.paddle = later.paddle;
        out.travel = travel.plus(later.travel);
        out.range_fault = range_fault || later.range_fault;
        return out;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return !paddle && travel.none() && !range_fault;
    }

    friend constexpr bool operator==(const DialReport&, const DialReport&) = default;
};

}
