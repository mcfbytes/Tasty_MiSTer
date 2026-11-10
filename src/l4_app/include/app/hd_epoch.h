// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/hd_flip_seq.h"

namespace mister::app {

struct HdEpoch {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr HdEpoch next() const noexcept { return HdEpoch{v + 1u}; }

    constexpr bool operator==(const HdEpoch&) const noexcept = default;
};

}
