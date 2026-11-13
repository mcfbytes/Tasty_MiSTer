// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace mister::proto {

enum class OsdRotation : std::uint8_t { None = 0, Left = 1, Right = 3 };

inline constexpr std::array<OsdRotation, 3> kRotationByIni{
    OsdRotation::None,
    OsdRotation::Right,
    OsdRotation::Left
};

[[nodiscard]] constexpr OsdRotation osd_rotation_of_ini(std::uint8_t osd_rotate) noexcept {
    return osd_rotate < kRotationByIni.size() ? kRotationByIni[osd_rotate] : OsdRotation::None;
}

[[nodiscard]] constexpr std::array<std::uint16_t, 1> rotation_words(OsdRotation rotate) noexcept {
    return {static_cast<std::uint16_t>(rotate)};
}

}
