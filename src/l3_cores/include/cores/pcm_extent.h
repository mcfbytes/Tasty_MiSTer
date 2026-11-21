// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "proto/types.h"

namespace mister::cores {

struct PcmExtent {

    std::uint64_t bytes = 0;

    std::optional<proto::FileSize> loop{};
};

}
