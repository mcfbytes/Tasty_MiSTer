// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace mister::cores::n64 {

enum class SystemType : std::uint32_t {
    Ntsc = 0,
    Pal,
    Unknown,
};

enum class Cic : std::uint32_t {
    Nus6101 = 0,
    Nus6102,
    Nus7101,
    Nus7102,
    Nus6103,
    Nus7103,
    Nus6105,
    Nus7105,
    Nus6106,
    Nus7106,
    Nus8303,
    Nus8401,
    Nus5167,
    NusDdus,
    Nus5101,
    Unknown,
};

enum class PadType : std::uint32_t {
    Pad = 0,
    Unplugged,
    PadWithCpak,
    PadWithRpak,
    Snac,
    PadWithTpak,
    Keyboard,
    Mouse,
};

[[nodiscard]] constexpr std::string_view system_text(SystemType v) noexcept {
    switch (v) {
        case SystemType::Ntsc:
            return "NTSC";
        case SystemType::Pal:
            return "PAL";
        case SystemType::Unknown:
            break;
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view cic_text(Cic v) noexcept {
    switch (v) {
        case Cic::Nus6101:
            return "6101";
        case Cic::Nus6102:
            return "6102";
        case Cic::Nus7101:
            return "7101";
        case Cic::Nus7102:
            return "7102";
        case Cic::Nus6103:
            return "6103";
        case Cic::Nus7103:
            return "7103";
        case Cic::Nus6105:
            return "6105";
        case Cic::Nus7105:
            return "7105";
        case Cic::Nus6106:
            return "6106";
        case Cic::Nus7106:
            return "7106";
        case Cic::Nus8303:
            return "8303";
        case Cic::Nus8401:
            return "8401";
        case Cic::Nus5167:
            return "5167";
        case Cic::NusDdus:
            return "DDUS";
        case Cic::Nus5101:
            return "5101";
        case Cic::Unknown:
            break;
    }
    return "Unknown";
}

}
