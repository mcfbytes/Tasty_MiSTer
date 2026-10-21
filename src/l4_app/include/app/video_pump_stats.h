// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

enum class VideoI2cOutcome : std::uint8_t {
    Unwired = 0,
    Absent,
    Ambiguous,
    Present,
    Configured,
    Verified,
    Noemit,
    Failed,
};

inline constexpr std::uint8_t kVideoModeNever = 0xFFu;
inline constexpr std::uint8_t kVideoModeCustom = 0xFEu;

struct VideoPumpStats {
    std::uint32_t edges = 0;
    std::uint32_t routed = 0;

    std::uint32_t empty_specs = 0;

    std::uint32_t fb_cmds_dropped = 0;
    std::uint32_t applies = 0;
    std::uint32_t abandoned = 0;
    std::uint32_t ini_reads = 0;
    std::uint32_t parse_fallbacks = 0;
    std::uint32_t solve_failures = 0;
    std::uint32_t zero_divider = 0;
    std::uint32_t approximated = 0;
    std::uint32_t stage_failures = 0;

    std::uint8_t last_mode_index = kVideoModeNever;
    std::uint32_t last_fpix_khz = 0;

    VideoI2cOutcome i2c = VideoI2cOutcome::Unwired;

    std::uint8_t i2c_bus = 0xFFu;
    std::uint32_t i2c_writes = 0;
    std::uint32_t i2c_errors = 0;
    std::uint8_t i2c_fail_reg = 0;

    std::uint8_t i2c_first_op = 0;
    std::uint8_t i2c_first_reg = 0;
    std::uint16_t i2c_first_errno = 0;
    std::uint16_t i2c_last_errno = 0;
    std::uint32_t i2c_verify_errors = 0;
    std::uint32_t i2c_poll_errors = 0;
    std::uint32_t i2c_reprobes = 0;
    std::uint32_t i2c_trips = 0;
    bool i2c_breaker_open = false;

    std::uint32_t spd_sends = 0;
    std::uint32_t spd_disables = 0;
    std::uint32_t spd_skips_dv = 0;
    std::uint32_t spd_noname = 0;
    std::uint8_t spd_fail_reg = 0;

    std::uint32_t filter_loads = 0;
    std::uint32_t family_switches = 0;
};

}
