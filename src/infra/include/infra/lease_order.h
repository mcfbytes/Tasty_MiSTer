// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::xthread {

class LeaseOrder {
    TASTY_SEAT_EXEMPT(component);

public:
    template <class G, SeatTag M>
    [[nodiscard]] static bool grant_unchanged(
        const Telemetry<G, M>& grant_cell,
        std::uint32_t held_gen
    ) noexcept {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        return grant_cell.generation() == held_gen;
    }

    template <class L, SeatTag H, class IsHeld>
    [[nodiscard]] static bool held_after_revoke(
        const Telemetry<L, H>& lease_cell,
        IsHeld is_held
    ) noexcept {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if (lease_cell.generation() == 0)
            return false;
        L body{};
        if (lease_cell.sample_into(body) == 0)
            return true;
        return is_held(body);
    }
};

}
