// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/thread_map.h"
#include "infra/error.h"
#include "rt_setup.h"

namespace mister::fw {

struct TopologyOutcome {
    long online = 0;
    RtSetup coverage{};
    RtSetup topology{};
    Ex<void> verdict{};
};

[[nodiscard]] TopologyOutcome verify_cpu_topology(
    const hal::ThreadMap& m,
    long online_cpus,
    RtMode mode
) noexcept;

[[nodiscard]] TopologyOutcome rt_topology_init(const hal::ThreadMap& m, RtMode mode) noexcept;

}
