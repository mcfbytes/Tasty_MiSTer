// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

enum class StOption : std::uint8_t {
    None = 0,

    FloppyA,
    FloppyB,
    WriteProtect,
    Cartridge,
    JoySwap,
    ModifyConfig,
    LoadConfig,
    SaveConfig,
    Mt32Pi,
    Reset,
    ColdBoot,

    Hdd0,
    Hdd1,
    Memory,
    Tos,
    Chipset,
    Blitter,
    Viking,
    Aspect,
    Screen,
    Mono60,
    VideoCrop,
    Scale,
    Scanlines,
    Dongle,
    YmAudio,
    kCount,
};

}
