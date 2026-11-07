// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "cores/mra_map_word.h"
#include "svc/types.h"

namespace mister::cores::mra {

enum class RomNodeKind : std::uint8_t { FilePart, TextPart, Patch, InterleaveStart, InterleaveEnd };

struct RomNode {
    RomNodeKind kind = RomNodeKind::FilePart;
    std::string name;
    std::string zips;
    svc::Crc32 crc{};
    std::int32_t offset = 0;
    std::int32_t length = -1;
    std::int32_t repeat = 1;
    MapWord map{};
    bool xor_op = false;
    std::string text;
    int input = 0;
    int output = 0;
};

}
