// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "app/types.h"
#include "cores/host_policy.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "svc/input/joy_plan.h"

namespace mister::app {

struct CoreInputFacts {

    CoreInputFacts() noexcept = default;

    static constexpr std::size_t kCoreNameCap = 32;
    FixedStr<kCoreNameCap, StrFit::Clip> core{};
    svc::input::JoyPlan plan{};

    infra::OptRef<const cores::HostPolicy> host{};

    [[nodiscard]] constexpr const cores::HostPolicy& policy() const noexcept {
        return cores::policy_of(host);
    }
};

using CoreInputCell = xthread::Telemetry<CoreInputFacts, SeatTag::Ui>;

}
