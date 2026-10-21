// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/counter.h"
#include "infra/seat.h"
#include "svc/device_report.h"
#include "svc/types.h"

namespace mister::svc {

class InputCounters {
public:
    TASTY_SEAT_RESIDENT(Input);

    void note_drain(const DeviceReport& r) noexcept {
        if (r.key_overflow != 0)
            n_key_overflow_.add(r.key_overflow);
        if (r.mouse_remainder != 0)
            n_mouse_rem_.add(r.mouse_remainder);
        if (r.axis_edges != 0)
            n_axis_edges_.add(r.axis_edges);
        if (r.quirk_drops != 0)
            n_quirk_drops_.add(r.quirk_drops);
    }

    void note_mask_recompute() noexcept { n_mask_recompute_.add(1); }

    void note_grab_failure() noexcept { n_grab_fail_.add(1); }

    void set_mapped(std::uint32_t n) noexcept { n_mapped_.set(n); }

    void set_bound(std::uint32_t bound, std::uint32_t live) noexcept {
        n_slotted_.set(bound);
        n_live_mask_.set(live);
    }

    void set_slot_hash(std::uint8_t player, std::uint32_t h) noexcept {
        n_slot_hash_[player].set(h);
    }

    void set_ghosts(std::uint32_t n) noexcept { n_ghost_.set(n); }

    std::uint32_t mapped_devices() const noexcept { return n_mapped_.get(); }

    std::uint32_t slotted_devices() const noexcept { return n_slotted_.get(); }

    std::uint32_t mask_recomputes() const noexcept { return n_mask_recompute_.get(); }

    std::uint32_t axis_edges() const noexcept { return n_axis_edges_.get(); }

    std::uint32_t mouse_remainder_bytes() const noexcept { return n_mouse_rem_.get(); }

    std::uint32_t key_overflows() const noexcept { return n_key_overflow_.get(); }

    std::uint32_t quirk_drops() const noexcept { return n_quirk_drops_.get(); }

    std::uint32_t grab_failures() const noexcept { return n_grab_fail_.get(); }

    std::uint32_t live_slot_mask() const noexcept { return n_live_mask_.get(); }

    std::uint32_t slot_key_hash(std::uint8_t player) const noexcept {
        return player < kMaxPlayers ? n_slot_hash_[player].get() : 0u;
    }

    std::uint32_t ghost_slots() const noexcept { return n_ghost_.get(); }

private:
    xthread::Counter n_mapped_{};
    xthread::Counter n_slotted_{};
    xthread::Counter n_mask_recompute_{};
    xthread::Counter n_axis_edges_{};
    xthread::Counter n_mouse_rem_{};
    xthread::Counter n_key_overflow_{};
    xthread::Counter n_quirk_drops_{};
    xthread::Counter n_grab_fail_{};
    xthread::Counter n_live_mask_{};
    xthread::Counter n_slot_hash_[kMaxPlayers]{};
    xthread::Counter n_ghost_{};
};

}
