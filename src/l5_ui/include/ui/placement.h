// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::ui {

enum class Placement : std::uint8_t {
    Slot,
    ResetGame,
    Cheats,
    SaveState,
    LoadState,
    CoreSettings,
    Controllers,
    CoreAbout,
    Hidden,
};

}
