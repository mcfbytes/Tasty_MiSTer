// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/park_timeout.h"
#include "infra/xthread/coworker.h"
#include "infra/prelude.h"

namespace mister::svc {

enum class LaneClass : std::uint8_t { Deadline, Bulk };

template <class L>
inline constexpr LaneClass lane_class_of = LaneClass::Deadline;
template <class L>
    requires requires { L::kLaneClass; }
inline constexpr LaneClass lane_class_of<L> = L::kLaneClass;

class IIoCoworker : public xthread::ICoworker {
public:
    [[nodiscard]] virtual bool idle() const noexcept = 0;

    [[nodiscard]] virtual bool quiescent() const noexcept { return idle(); }

    [[nodiscard]] virtual infra::ParkTimeout poll_period() const noexcept {
        return infra::kForever;
    }

protected:
    IIoCoworker() = default;
};

}
