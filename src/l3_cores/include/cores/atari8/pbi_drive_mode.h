// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mister::cores::atari8 {

enum class PbiDriveMode : std::uint8_t { Off, Pbi, Sio };

struct PbiModeBytes {
    PbiDriveMode mode;
    std::byte config;
    std::byte answer;
};

inline constexpr std::array<PbiModeBytes, 3> kPbiModeBytes = {
    PbiModeBytes{PbiDriveMode::Off, std::byte{0x00}, std::byte{0xFF}},
    PbiModeBytes{PbiDriveMode::Pbi, std::byte{0x01}, std::byte{0x00}},
    PbiModeBytes{PbiDriveMode::Sio, std::byte{0x02}, std::byte{0x01}}
};

[[nodiscard]] constexpr std::byte config_byte(PbiDriveMode mode) noexcept {
    for (const PbiModeBytes& row : kPbiModeBytes)
        if (row.mode == mode)
            return row.config;
    return kPbiModeBytes[0].config;
}

[[nodiscard]] constexpr std::byte answer_byte(PbiDriveMode mode) noexcept {
    for (const PbiModeBytes& row : kPbiModeBytes)
        if (row.mode == mode)
            return row.answer;
    return kPbiModeBytes[0].answer;
}

}
