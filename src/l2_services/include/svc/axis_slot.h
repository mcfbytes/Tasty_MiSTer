// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/axis_cal.h"
#include "svc/types.h"

namespace mister::svc {

struct AxisSlot {

    enum class Edge : std::uint8_t { none, low, high };

    AxisCal cal{};
    Edge edge = Edge::none;

    [[nodiscard]] static constexpr EvdevCode code_of(Edge e, EvdevCode axis) noexcept {
        return e == Edge::low ? axis.key_emu_min() : axis.key_emu_max();
    }
};

}
