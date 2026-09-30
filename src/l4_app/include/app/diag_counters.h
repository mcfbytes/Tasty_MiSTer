// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct DiagCounters {

    std::uint8_t link = 0;
    std::uint32_t core_shutdowns = 0;
    std::uint8_t stage_status_change = 0;

    std::uint32_t prefetch_park_timeouts = 0;

    std::uint32_t disc_sync_decompress = 0;
    std::uint32_t disc_park_timeouts = 0;
    std::uint32_t disc_prefetch_refusals = 0;

    std::uint16_t last_error_code = 0;
    std::uint16_t last_error_site = 0;

    std::uint32_t last_error_detail = 0;

    std::uint8_t ladder_live = 0;

    std::uint8_t ladder_pc = 0;
    std::uint32_t ladder_rungs = 0;
    std::uint64_t ladder_bytes = 0;
    std::uint32_t ladder_assets = 0;
    bool ladder_bios_found = false;
    bool ladder_disc_mounted = false;
    bool ladder_save_mounted = false;

    std::uint32_t file_tx_count = 0;
    std::uint64_t file_tx_bytes = 0;
    std::uint16_t file_tx_index = 0;

    std::uint32_t file_tx_window = 0;
    std::uint32_t file_tx_window_refusals = 0;
    std::uint16_t file_tx_window_refusal_code = 0;

    std::uint32_t blk_rounds = 0;
    std::uint32_t blk_served = 0;
    std::uint32_t blk_blocks = 0;
    std::uint32_t blk_errors = 0;
    std::uint16_t blk_err_code = 0;
    std::uint32_t blk_oversize = 0;
    std::uint32_t blk_unencodable = 0;
    std::uint32_t blk_blank_filled = 0;
    std::uint32_t blk_write_failures = 0;
    std::uint32_t blk_stock = 0;
    std::uint32_t blk_config = 0;
    std::uint32_t blk_discarded = 0;

    std::uint32_t blk_deferred = 0;
    std::uint32_t blk_expired = 0;
    std::uint32_t blk_pf_expired = 0;
    std::uint32_t blk_staging_expired = 0;
    std::uint32_t blk_stale = 0;

    std::uint32_t cd_flow_waits = 0;
    std::uint32_t cd_cdda_sectors = 0;

    std::uint32_t cd_data_sectors = 0;
    std::uint32_t cd_idle_ticks = 0;

    std::uint32_t cd_gated_ticks = 0;
    std::uint32_t cd_drive_track = 0;
    std::int32_t cd_drive_lba = 0;
    std::int32_t cd_drive_audio_lba = 0;

    std::uint32_t cd_substitutes = 0;
    std::uint32_t cd_subcode_substitutes = 0;
    std::uint32_t cd_not_resident = 0;

    std::uint32_t cd_busy_ticks = 0;
    std::uint32_t cd_egress_abandons = 0;
    std::uint8_t cd_drive_state = 0;
    bool cd_drive_is_data = true;
    std::uint32_t status_reply_refusals = 0;

    std::uint32_t stale_owner_steps = 0;
    std::uint32_t start_answer_drops = 0;

    std::uint32_t core_abandons = 0;
};

using DiagCountersCell = xthread::Telemetry<DiagCounters, SeatTag::RT>;

}  // namespace mister::app
