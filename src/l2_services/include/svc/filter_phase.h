// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace mister::svc {

struct FilterPhase {
    std::array<std::int16_t, 4> t{};
    friend constexpr bool operator==(const FilterPhase&, const FilterPhase&) = default;
};

}
