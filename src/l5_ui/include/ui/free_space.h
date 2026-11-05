// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "proto/types.h"

namespace mister::ui {

enum class FreeSpaceStyle : std::uint8_t { Classic, Hd };

[[nodiscard]] std::string format_free_space(proto::FileSize avail, FreeSpaceStyle style);

}
