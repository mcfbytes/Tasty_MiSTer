// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/types.h"

namespace mister::proto {

struct AxiLwDecl {
    hal::LwOffset offset;
    std::uint32_t size;

    [[nodiscard]] constexpr bool holds(hal::LwOffset abs) const noexcept {
        return offset <= abs && abs.distance_to(offset) < size;
    }

    friend constexpr bool operator==(AxiLwDecl, AxiLwDecl) = default;
};

}
