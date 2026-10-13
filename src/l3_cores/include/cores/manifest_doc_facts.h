// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "proto/rotation_dir.h"

namespace mister::cores {

struct ManifestDocFacts {
    bool vertical = false;
    proto::RotationDir rotation = proto::RotationDir::None;
    std::string setname;
    bool setname_same_dir = false;
};

}  // namespace mister::cores
