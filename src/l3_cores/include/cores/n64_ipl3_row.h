// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cores/n64_db_tags.h"

namespace mister::cores::n64 {

struct Ipl3Row {
    std::uint64_t sum = 0;
    Cic ntsc = Cic::Unknown;
    Cic pal = Cic::Unknown;
    SystemType force = SystemType::Unknown;
    std::string_view provenance{};
};

inline constexpr std::array<Ipl3Row, 26> kIpl3Table = {{
    {UINT64_C(0x000000a316adc55a),
     Cic::Nus6102,
     Cic::Nus7101,
     SystemType::Unknown,
     "CIC-6102/7101 IPL3"},
    {UINT64_C(0x000000a30dacd530),
     Cic::Nus6102,
     Cic::Nus7101,
     SystemType::Unknown,
     "NOP:ed out CRC check"},
    {UINT64_C(0x000000039c981107),
     Cic::Nus6102,
     Cic::Nus7101,
     SystemType::Unknown,
     "hcs64's CIC-6102 IPL3 replacement"},
    {UINT64_C(0x000000d2828281b0),
     Cic::Nus6102,
     Cic::Nus7101,
     SystemType::Unknown,
     "Unknown. Used in some homebrew"},
    {UINT64_C(0x000000d2531456f6),
     Cic::Nus6102,
     Cic::Nus7101,
     SystemType::Unknown,
     "Unknown. Used in some homebrew"},
    {UINT64_C(0x000000d28bc5f6ae),
     Cic::Nus6102,
     Cic::Nus7101,
     SystemType::Unknown,
     "Unknown. Used in some homebrew"},
    {UINT64_C(0x000000d2be3c4486),
     Cic::Nus6102,
     Cic::Nus7101,
     SystemType::Unknown,
     "Xeno Crisis custom IPL3"},
    {UINT64_C(0x0000009acc31e644),
     Cic::Nus6102,
     Cic::Nus7101,
     SystemType::Unknown,
     "HW1 IPL3 (Turok E3 prototype)"},
    {UINT64_C(0x0000009474732e6b),
     Cic::Nus6102,
     Cic::Nus7101,
     SystemType::Unknown,
     "IPL3 re-assembled with the GNU assembler (iQue)"},
    {UINT64_C(0x000000a405397b05), Cic::Nus7102, Cic::Nus7102, SystemType::Pal, "CIC-7102 IPL3"},
    {UINT64_C(0x000000a3fc388adb),
     Cic::Nus7102,
     Cic::Nus7102,
     SystemType::Pal,
     "NOP:ed out CRC check"},
    {UINT64_C(0x000000a0f26f62fe), Cic::Nus6101, Cic::Nus6101, SystemType::Ntsc, "CIC-6101 IPL3"},
    {UINT64_C(0x000000a0e96e72d4),
     Cic::Nus6101,
     Cic::Nus6101,
     SystemType::Ntsc,
     "NOP:ed out CRC check"},
    {UINT64_C(0x000000a9229d7c45),
     Cic::Nus6103,
     Cic::Nus7103,
     SystemType::Unknown,
     "CIC-x103 IPL3"},
    {UINT64_C(0x000000a9199c8c1b),
     Cic::Nus6103,
     Cic::Nus7103,
     SystemType::Unknown,
     "NOP:ed out CRC check"},
    {UINT64_C(0x000000271316d406),
     Cic::Nus6103,
     Cic::Nus7103,
     SystemType::Unknown,
     "All zeros bar font (iQue Paper Mario)"},
    {UINT64_C(0x000000f8b860ed00),
     Cic::Nus6105,
     Cic::Nus7105,
     SystemType::Unknown,
     "CIC-x105 IPL3"},
    {UINT64_C(0x000000f8af5ffcd6),
     Cic::Nus6105,
     Cic::Nus7105,
     SystemType::Unknown,
     "NOP:ed out CRC check"},
    {UINT64_C(0x000000ba5ba4b8cd),
     Cic::Nus6106,
     Cic::Nus7106,
     SystemType::Unknown,
     "CIC-x106 IPL3"},
    {UINT64_C(0x0000012daafc8aab), Cic::Nus5167, Cic::Nus5167, SystemType::Unknown, "CIC-5167"},
    {UINT64_C(0x000000a9df4b39e1), Cic::Nus8303, Cic::Nus8303, SystemType::Unknown, "CIC-8303"},
    {UINT64_C(0x000000aa764e39e1), Cic::Nus8401, Cic::Nus8401, SystemType::Unknown, "CIC-8401"},
    {UINT64_C(0x000000abb0b739e1), Cic::NusDdus, Cic::NusDdus, SystemType::Unknown, "CIC-DDUS"},
    {UINT64_C(0x00000081ce470326),
     Cic::Nus5101,
     Cic::Nus5101,
     SystemType::Unknown,
     "CIC-5101 IPL3"},
    {UINT64_C(0x000000827a47195a),
     Cic::Nus5101,
     Cic::Nus5101,
     SystemType::Unknown,
     "Kuru Kuru Fever"},
    {UINT64_C(0x00000082551e4848),
     Cic::Nus5101,
     Cic::Nus5101,
     SystemType::Unknown,
     "Tower & Shaft"},
}};

inline constexpr Ipl3Row kIpl3Default{
    0,
    Cic::Nus6102,
    Cic::Nus7101,
    SystemType::Unknown,
    "unknown signature, uses default"
};

[[nodiscard]] constexpr const Ipl3Row* find_ipl3(std::uint64_t sum) noexcept {
    for (const auto& row : kIpl3Table)
        if (row.sum == sum)
            return &row;
    return nullptr;
}

}
