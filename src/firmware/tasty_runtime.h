// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "os/process_name.h"
#include "tasty_paths.h"

namespace mister::fw {

struct TastyRuntime {
    TastyPaths paths{};
    os::ProcessName owner = kOwnerName;
    os::ProcessName stock = kStockName;
};

}
