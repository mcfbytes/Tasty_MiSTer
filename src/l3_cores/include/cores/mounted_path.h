// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "cores/types.h"

namespace mister::cores {

struct MountedPath {
    std::string_view path;
    FileSize size_bytes{};
};

}
