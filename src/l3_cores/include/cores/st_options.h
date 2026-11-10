// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "cores/types.h"

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

[[nodiscard]] constexpr OptionTag tag_of(StOption o) noexcept {
    return OptionTag{static_cast<std::uint8_t>(o)};
}

[[nodiscard]] constexpr std::optional<StOption> option_of(OptionTag id) noexcept {
    if (id.v >= static_cast<std::uint8_t>(StOption::kCount))
        return std::nullopt;
    return static_cast<StOption>(id.v);
}

}
