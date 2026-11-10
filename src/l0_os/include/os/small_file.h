// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "infra/error.h"

namespace mister::os {

[[nodiscard]] Ex<std::span<char>> read_small_file(const char* path, std::span<char> buf) noexcept;

}
