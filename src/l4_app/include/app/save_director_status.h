// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/error.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "svc/types.h"

namespace mister::app {

struct SaveDirectorStatus {
    bool probing = false;
    bool holding = false;
    svc::Tally confirmed{};
    svc::Tally direct{};
    svc::Tally deferred{};
    svc::Tally forced{};
    svc::Tally fault_episodes{};
    std::optional<Error> last_fault{};

    friend constexpr bool operator==(const SaveDirectorStatus&, const SaveDirectorStatus&) =
        default;
};

using SaveDirectorCell = xthread::Telemetry<SaveDirectorStatus, SeatTag::Main>;

}
