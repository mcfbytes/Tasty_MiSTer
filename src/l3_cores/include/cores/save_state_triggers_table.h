// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

#include "proto/bit_field.h"

namespace mister::cores::detail {

using proto::BitField;
using proto::StatusBit;

struct Row {
    std::string_view name;
    BitField save;
    BitField restore;
    BitField slot;
};

inline constexpr std::array<Row, 31> kRows{{
    {"Apple-II",
     BitField{StatusBit{48}, 1},
     BitField{StatusBit{49}, 1},
     BitField{StatusBit{46}, 2}},
    {"AtariLynx",
     BitField{StatusBit{28}, 1},
     BitField{StatusBit{29}, 1},
     BitField{StatusBit{37}, 2}},
    {"BoogieWings",
     BitField{StatusBit{94}, 1},
     BitField{StatusBit{95}, 1},
     BitField{StatusBit{61}, 5}},
    {"Cave", BitField{StatusBit{44}, 1}, BitField{StatusBit{45}, 1}, BitField{StatusBit{41}, 2}},
    {"Darius",
     BitField{StatusBit{109}, 1},
     BitField{StatusBit{110}, 1},
     BitField{StatusBit{105}, 4}},
    {"GAMEBOY", BitField{StatusBit{28}, 1}, BitField{StatusBit{29}, 1}, BitField{StatusBit{32}, 2}},
    {"GBA", BitField{StatusBit{17}, 1}, BitField{StatusBit{18}, 1}, BitField{StatusBit{37}, 2}},
    {"Gamecom", BitField{StatusBit{42}, 1}, BitField{StatusBit{43}, 1}, BitField{StatusBit{45}, 2}},
    {"IGSPGM", BitField{StatusBit{43}, 1}, BitField{StatusBit{44}, 1}, BitField{StatusBit{41}, 2}},
    {"Loopy", BitField{StatusBit{20}, 1}, BitField{StatusBit{21}, 1}, BitField{StatusBit{18}, 2}},
    {"M72", BitField{StatusBit{43}, 1}, BitField{StatusBit{44}, 1}, BitField{StatusBit{41}, 2}},
    {"N64", BitField{}, BitField{}, BitField{}},
    {"NES", BitField{StatusBit{42}, 1}, BitField{StatusBit{43}, 1}, BitField{StatusBit{45}, 2}},
    {"NGPC", BitField{StatusBit{42}, 1}, BitField{StatusBit{43}, 1}, BitField{StatusBit{45}, 2}},
    {"NightSlashers",
     BitField{StatusBit{94}, 1},
     BitField{StatusBit{95}, 1},
     BitField{StatusBit{62}, 4}},
    {"Oric", BitField{}, BitField{}, BitField{}},
    {"PSX", BitField{StatusBit{17}, 1}, BitField{StatusBit{18}, 1}, BitField{StatusBit{37}, 2}},
    {"PocketStation",
     BitField{StatusBit{42}, 1},
     BitField{StatusBit{43}, 1},
     BitField{StatusBit{45}, 2}},
    {"Raiden",
     BitField{StatusBit{110}, 1},
     BitField{StatusBit{111}, 1},
     BitField{StatusBit{105}, 5}},
    {"Raiden2",
     BitField{StatusBit{110}, 1},
     BitField{StatusBit{111}, 1},
     BitField{StatusBit{105}, 5}},
    {"SGB", BitField{StatusBit{30}, 1}, BitField{StatusBit{31}, 1}, BitField{StatusBit{51}, 2}},
    {"SMS", BitField{StatusBit{61}, 1}, BitField{StatusBit{62}, 1}, BitField{StatusBit{15}, 2}},
    {"SNES", BitField{StatusBit{47}, 1}, BitField{StatusBit{48}, 1}, BitField{StatusBit{51}, 2}},
    {"SuperAcan",
     BitField{StatusBit{20}, 1},
     BitField{StatusBit{21}, 1},
     BitField{StatusBit{18}, 2}},
    {"TRS-80", BitField{StatusBit{18}, 1}, BitField{StatusBit{17}, 1}, BitField{StatusBit{19}, 2}},
    {"TaitoAsuka",
     BitField{StatusBit{109}, 1},
     BitField{StatusBit{110}, 1},
     BitField{StatusBit{105}, 4}},
    {"TaitoF2", BitField{StatusBit{43}, 1}, BitField{StatusBit{44}, 1}, BitField{StatusBit{41}, 2}},
    {"Tamagotchi",
     BitField{StatusBit{28}, 1},
     BitField{StatusBit{29}, 1},
     BitField{StatusBit{37}, 2}},
    {"VirtualBoy",
     BitField{StatusBit{42}, 1},
     BitField{StatusBit{43}, 1},
     BitField{StatusBit{45}, 2}},
    {"WonderSwan",
     BitField{StatusBit{28}, 1},
     BitField{StatusBit{29}, 1},
     BitField{StatusBit{37}, 2}},
    {"cavebanpresto",
     BitField{StatusBit{47}, 1},
     BitField{StatusBit{48}, 1},
     BitField{StatusBit{44}, 2}},
}};

}
