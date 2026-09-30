// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::proto {

enum class VolumeCmd : std::uint8_t {
    Relative,
    SetMute,
    SetAtten,
    ToggleMute,
};

}
