// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_manifest.h"
#include "cores/host_services.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

struct CoreGrant {
    const svc::Vfs& vfs;
    const HostServices& services;
    CoreManifest manifest{};
};

}
