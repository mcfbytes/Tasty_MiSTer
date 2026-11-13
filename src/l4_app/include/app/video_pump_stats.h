// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/types.h"
#include "os/types.h"
#include "infra/telemetry.h"
#include "svc/types.h"

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

[[nodiscard]] constexpr const char* video_i2c_outcome_name(VideoI2cOutcome o) noexcept {
    switch (o) {
        case VideoI2cOutcome::Unwired:
            return "unwired";
        case VideoI2cOutcome::Absent:
            return "absent";
        case VideoI2cOutcome::Ambiguous:
            return "ambiguous";
        case VideoI2cOutcome::Present:
            return "present";
        case VideoI2cOutcome::Configured:
            return "configured";
        case VideoI2cOutcome::Verified:
            return "verified";
        case VideoI2cOutcome::Noemit:
            return "noemit";
        case VideoI2cOutcome::Failed:
            return "failed";
    }
    return "unknown";
}

inline constexpr std::uint8_t kI2cBusNeverAcked = 0xFFu;

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

    VideoModeIndex last_mode_index = kVideoModeIndexNever;
    std::uint32_t last_fpix_khz = 0;

    VideoI2cOutcome i2c = VideoI2cOutcome::Unwired;

    std::uint8_t i2c_bus = kI2cBusNeverAcked;
    svc::Tally i2c_writes{};
    svc::Tally i2c_errors{};
    std::optional<os::I2cReg> i2c_fail_reg{};

    std::uint8_t i2c_first_op = 0;
    std::optional<os::I2cReg> i2c_first_reg{};
    svc::OsErrno i2c_first_errno{};
    svc::OsErrno i2c_last_errno{};
    std::uint32_t i2c_verify_errors = 0;
    std::uint32_t i2c_poll_errors = 0;
    std::uint32_t i2c_reprobes = 0;
    svc::Tally i2c_trips{};
    bool i2c_breaker_open = false;

    std::uint32_t spd_sends = 0;
    std::uint32_t spd_disables = 0;
    std::uint32_t spd_dv1_sends = 0;
    svc::Tally spd_dv1_nowidth{};
    std::uint32_t spd_noname = 0;
    std::optional<os::I2cReg> spd_fail_reg{};

    std::uint32_t filter_loads = 0;
    std::uint32_t family_switches = 0;
};

[[nodiscard]] inline os::I2cReg reg_for_print(std::optional<os::I2cReg> reg) noexcept {
    return reg.value_or(os::I2cReg{});
}

using VideoStatsCell = xthread::Telemetry<VideoPumpStats, SeatTag::Ui>;

}
