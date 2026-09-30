// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/fabric_region.h"

namespace mister::cores {

struct CoreWindowDecl {
    hal::FabricRegion region;
    bool feeds_pcm = false;
};

}  // namespace mister::cores
