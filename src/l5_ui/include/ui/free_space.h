// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace mister::ui {

enum class FreeSpaceStyle : std::uint8_t { Classic, Hd };

[[nodiscard]] std::string format_free_space(std::uint64_t avail_bytes, FreeSpaceStyle style);

}
