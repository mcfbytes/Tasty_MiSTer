// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "infra/error.h"

namespace mister::os {

[[nodiscard]] Ex<std::size_t> read_small_file(const char* path, std::span<char> buf) noexcept;

}
