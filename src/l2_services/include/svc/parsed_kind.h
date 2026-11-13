// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::svc::video_timing {

enum class ParsedKind : std::uint8_t { PresetPick, CustomMode, kCount };

[[nodiscard]] constexpr std::size_t arm_index(ParsedKind k) noexcept {
    return static_cast<std::size_t>(k);
}

}
