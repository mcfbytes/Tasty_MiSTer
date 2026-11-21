// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <optional>

#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"

namespace mister::app {

struct AutosaveKnobs {
    bool enabled = false;
    std::optional<std::chrono::seconds> saved_toast{};

    friend constexpr bool operator==(const AutosaveKnobs&, const AutosaveKnobs&) = default;
};

using AutosaveKnobsCell = xthread::Telemetry<AutosaveKnobs, SeatTag::Main>;

}
