// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

enum class MountState : std::uint8_t { Done, Pending, Failed };

}
