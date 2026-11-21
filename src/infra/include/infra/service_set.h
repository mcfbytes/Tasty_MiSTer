// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/service_slot.h"

namespace mister::infra {

class ServiceSet {

    TASTY_SEAT_EXEMPT(component);

public:
    constexpr ServiceSet() noexcept = default;

    [[nodiscard]] constexpr ServiceSet with(ServiceSlot slot) const noexcept {
        ServiceSet s = *this;
        s.bits_ = bits_ | slot.bit();
        return s;
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return bits_ == 0u; }

    [[nodiscard]] constexpr std::uint32_t mask() const noexcept { return bits_; }

    friend constexpr bool operator==(ServiceSet, ServiceSet) = default;

private:
    std::uint32_t bits_ = 0;
};

}
