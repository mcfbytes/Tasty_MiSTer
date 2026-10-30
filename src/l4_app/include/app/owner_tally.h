// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::app {

struct OwnerTally {
    TASTY_SEAT_RESIDENT(Main);

    std::uint32_t reboot_refusals = 0;
    std::uint32_t companion_walks = 0;
    std::uint32_t companion_skips = 0;

    std::uint32_t companion_attach_refusals = 0;

    std::uint32_t manifest_errors = 0;

    std::uint32_t loads_refused_busy = 0;

    std::uint32_t belt_offers_refused = 0;

    std::uint32_t savestate_refusals = 0;

    std::uint32_t savestate_hold_expiries = 0;
    std::uint32_t order_refusals = 0;

    std::uint32_t stale_events = 0;
    std::uint32_t core_make_refusals = 0;

    std::uint32_t ui_request_unhandled = 0;
    std::uint32_t ui_request_misrouted = 0;

    std::uint32_t link_event_unhandled = 0;
    std::uint32_t link_event_misrouted = 0;

    std::uint32_t fabric_wedges = 0;

    std::uint32_t mgl_row0 = 0;

    std::uint32_t files_loaded = 0;
    std::uint32_t start_assets_ordered = 0;

    std::uint32_t remembered_ordered = 0;
    std::uint32_t remembered_missed = 0;
    std::uint32_t files_failed = 0;

    std::uint32_t addons_sent = 0;
    std::uint32_t addons_missing = 0;

    std::uint32_t ram_images_sent = 0;
    std::uint32_t ram_images_declined = 0;

    std::uint32_t images_sized = 0;

    std::uint32_t config_saves = 0;

    std::uint32_t save_write_failures = 0;

    std::uint32_t ini_write_failures = 0;

    std::uint32_t memsz_unmeasured = 0;

    std::uint32_t sdram_write_failures = 0;
    std::uint32_t op_drops = 0;
    std::uint32_t identities_ok = 0;
    std::uint32_t identities_fail = 0;

    std::uint32_t identity_intern_refusals = 0;

    std::uint32_t doorbell_intern_refusals = 0;

    std::uint32_t ladders_run = 0;
    std::uint32_t ladder_failures = 0;

    std::uint32_t resets_ordered = 0;

    std::uint32_t open_saves_armed = 0;
    std::uint32_t open_saves_skipped = 0;
};

}
