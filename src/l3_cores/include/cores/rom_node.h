// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace mister::cores::mra {

enum class RomNodeKind : std::uint8_t { FilePart, TextPart, Patch, InterleaveStart, InterleaveEnd };

struct RomNode {
    RomNodeKind kind = RomNodeKind::FilePart;
    std::string name;
    std::string zips;
    std::uint32_t crc = 0;
    std::int32_t offset = 0;
    std::int32_t length = -1;
    std::int32_t repeat = 1;
    std::uint32_t map = 0;
    std::uint32_t map_digits = 0;
    bool xor_op = false;
    std::string text;
    int input = 0;
    int output = 0;
};

}  // namespace mister::cores::mra
