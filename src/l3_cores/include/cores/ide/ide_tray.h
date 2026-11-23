// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores::ide {

enum class Tray : std::uint8_t { Fixed, Empty, Loaded };

}
