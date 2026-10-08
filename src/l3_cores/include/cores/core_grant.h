// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_manifest.h"
#include "cores/host_services.h"

namespace mister::cores {

struct CoreGrant {
    const HostServices& services;
    CoreManifest manifest{};
};

}  // namespace mister::cores
