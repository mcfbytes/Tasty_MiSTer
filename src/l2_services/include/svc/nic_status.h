// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "infra/xthread/telemetry.h"
#include "svc/nic_link.h"
#include "svc/nic_poll.h"

namespace mister::svc {

struct NicStatus {
    NicLink link = NicLink::Off;
    bool up = false;

    infra::Tally refused_unmeasured{};
    infra::Tally refused_link{};
    infra::Tally start_failures{};
    NicPoll totals{};

    friend constexpr bool operator==(const NicStatus&, const NicStatus&) = default;
};

using NicStatusCell = infra::xthread::Telemetry<NicStatus, SeatTag::Nic>;

}
