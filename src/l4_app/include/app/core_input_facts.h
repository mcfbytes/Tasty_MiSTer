// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "app/types.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "svc/analog_reshape.h"
#include "svc/joy_plan.h"

namespace mister::app {

struct CoreInputFacts {

    CoreInputFacts() noexcept = default;

    static constexpr std::size_t kCoreNameCap = 32;
    FixedStr<kCoreNameCap, StrFit::Clip> core{};
    bool front_end = false;
    svc::JoyPlan plan{};
    infra::OptRef<const svc::IAnalogReshape> reshape{};
};

using CoreInputCell = xthread::Telemetry<CoreInputFacts, SeatTag::Ui>;

}
