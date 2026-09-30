// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::proto {

enum class SaveKind : std::uint8_t { Dips = 1, Slot = 2, SlotNames = 3, Nvram = 4 };

enum class SaveStatus : std::uint8_t { Bytes = 0, Refused = 1, Nothing = 2 };

}  // namespace mister::proto
