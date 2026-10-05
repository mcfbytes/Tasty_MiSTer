// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/launcher_profile.h"
#include "infra/fixed_str.h"
#include "infra/telemetry.h"

namespace mister::app {

struct LauncherDemand {

    const LauncherProfile* profile = nullptr;
    FixedStr<256, StrFit::Reject> program{};
    std::uint32_t core_gen = 0;
    bool front_end = false;
    bool direct_video = false;

    bool scanout_core = false;
};

using LauncherDemandCell = xthread::Telemetry<LauncherDemand>;

}  // namespace mister::app
