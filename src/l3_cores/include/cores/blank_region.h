// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "proto/types.h"

namespace mister::cores {

struct BlankRegion {
    proto::Lba block;
    std::uint32_t within;
    std::span<const std::byte> bytes;
};

}
