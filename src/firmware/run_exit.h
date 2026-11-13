// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "infra/json_out.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "os/child_process.h"

namespace mister::fw {

enum class ExitCode : std::uint8_t {
    Done = 0,
    Refused = 1,
    BusyOwner = 2,
    RomMismatch = 3,
    Wedged = 75,
};

inline constexpr os::ChildProcessExit::SignalNumber::StatusBase kSignalBase{128};

using RunExit = ExitCode;

[[nodiscard]] constexpr RunExit refused() noexcept {
    return ExitCode::Refused;
}

[[nodiscard]] constexpr RunExit busy_owner() noexcept {
    return ExitCode::BusyOwner;
}

[[nodiscard]] constexpr RunExit rom_mismatch() noexcept {
    return ExitCode::RomMismatch;
}

[[nodiscard]] constexpr RunExit after_signal(os::ChildProcessExit::SignalNumber s) noexcept {
    return static_cast<ExitCode>(s.shell_status(kSignalBase));
}

[[nodiscard]] constexpr bool failed(RunExit code) noexcept {
    return code != ExitCode::Done;
}

[[nodiscard]] constexpr int process_status(RunExit code) noexcept {
    return std::to_underlying(code);
}

[[nodiscard]] constexpr std::optional<int> as_exit(RunExit code) noexcept {
    return process_status(code);
}

using RunExitCell = xthread::Telemetry<RunExit, SeatTag::Ui>;

}
