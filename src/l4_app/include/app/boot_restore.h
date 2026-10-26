// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

enum class BootRestore : std::uint8_t { None, Asked, Laddered };

}
