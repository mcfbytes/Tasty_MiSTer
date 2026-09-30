// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "proto/types.h"

namespace mister::cores {

using proto::FileSize;
using proto::PathId;

struct MountedPath {
    PathId path_id{};
    std::string_view display;
    FileSize size_bytes{};
};

}  // namespace mister::cores
