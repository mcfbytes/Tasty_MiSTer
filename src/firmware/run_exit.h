// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "infra/json_out.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::fw {

enum class ExitCode : std::uint8_t {
    Done = 0,
    Refused = 1,
    Wedged = 75,
};

constexpr void json_field(infra::JsonOut& o, infra::JsonName key, ExitCode code) noexcept {
    o.field(key, std::to_underlying(code));
}

using RunExit = ExitCode;

[[nodiscard]] constexpr RunExit refused() noexcept {
    return ExitCode::Refused;
}

[[nodiscard]] constexpr bool failed(RunExit code) noexcept {
    return code != ExitCode::Done;
}

[[nodiscard]] constexpr int process_status(RunExit code) noexcept {
    return static_cast<int>(std::to_underlying(code));
}

using RunExitCell = xthread::Telemetry<RunExit, SeatTag::Ui>;

}
