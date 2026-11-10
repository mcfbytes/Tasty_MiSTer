// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "svc/device_report.h"
#include "svc/input_service_counts.h"
#include "svc/types.h"

namespace mister::svc {

class InputCounters {
public:
    TASTY_SEAT_RESIDENT(Input);

    void note_drain(const DeviceReport& r) noexcept {
        if (r.keys.dropped() != 0)
            counts_.key_overflows.add(Tally{r.keys.dropped()});
        if (r.mouse_remainder != 0)
            counts_.mouse_remainder_bytes.add(Tally{r.mouse_remainder});
        if (r.axis_edges != 0)
            counts_.axis_edges.add(Tally{r.axis_edges});
        if (r.quirk_drops != 0)
            counts_.quirk_drops.add(Tally{r.quirk_drops});
    }

    void note_mask_recompute() noexcept { counts_.mask_recomputes.add(); }

    void note_grab_failure() noexcept { counts_.grab_failures.add(); }

    void set_mapped(std::uint32_t n) noexcept { counts_.mapped.set(Tally{n}); }

    void set_bound(std::uint32_t bound, PlayerMask live) noexcept {
        counts_.slotted.set(Tally{bound});
        counts_.live = live;
    }

    void set_slot_hash(PlayerIndex player, KeyHash h) noexcept {
        if (const auto slot = counts_.slot_hash.find(player))
            *slot = h;
    }

    void set_ghosts(std::uint32_t n) noexcept { counts_.ghost_slots.set(Tally{n}); }

    [[nodiscard]] const InputServiceCounts& record() const noexcept { return counts_; }

private:
    InputServiceCounts counts_{};
};

}
