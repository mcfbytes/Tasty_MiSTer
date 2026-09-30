// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace mister::cores {

struct BlankRegion {
    std::uint32_t offset_bytes;
    std::span<const std::byte> bytes;
};

}  // namespace mister::cores
