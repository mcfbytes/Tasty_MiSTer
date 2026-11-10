// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

enum class CdDriveState : std::uint8_t {
    NoDisc = 0,
    Open,
    Stopped,
    Toc,
    Seeking,
    Reading,
    Playing,
    Scanning,
    Paused,
    End,
};

}
