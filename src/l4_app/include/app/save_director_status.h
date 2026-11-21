// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "infra/tally.h"
#include "svc/types.h"

namespace mister::app {

struct SaveDirectorStatus {
    bool probing = false;
    bool holding = false;
    infra::Tally confirmed{};
    infra::Tally direct{};
    infra::Tally deferred{};
    infra::Tally forced{};
    infra::Tally fault_episodes{};
    std::optional<Error> last_fault{};

    friend constexpr bool operator==(const SaveDirectorStatus&, const SaveDirectorStatus&) =
        default;
};

using SaveDirectorCell = xthread::Telemetry<SaveDirectorStatus, SeatTag::Main>;

}
