// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "proto/rotation_dir.h"

namespace mister::cores {

struct ManifestDocFacts {
    bool vertical = false;
    proto::RotationDir rotation = proto::RotationDir::None;
    std::string setname;
    bool setname_same_dir = false;
};

struct ManifestDocRole {
    std::uint64_t doc_max;
    std::string_view (*root_of)(std::string_view manifest_rel) noexcept;
    ManifestDocFacts (*facts_of)(std::string_view doc);
};

}  // namespace mister::cores
