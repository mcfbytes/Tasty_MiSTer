// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "cores/types.h"

namespace mister::cores::archie {

enum class ArchieOption : std::uint8_t {
    None = 0,
    Floppy0,
    Floppy1,
    OsRom,
    Aspect,
    Scale,
    Refresh,
    StereoMix,
    AudioFix,
    JoySwap,
    kCount,
};

[[nodiscard]] constexpr OptionTag tag_of(ArchieOption o) noexcept {
    return OptionTag{static_cast<std::uint8_t>(o)};
}

[[nodiscard]] constexpr std::optional<ArchieOption> option_of(OptionTag id) noexcept {
    if (id.v >= static_cast<std::uint8_t>(ArchieOption::kCount))
        return std::nullopt;
    return static_cast<ArchieOption>(id.v);
}

}
