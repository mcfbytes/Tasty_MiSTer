// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cores/rom_node.h"

namespace mister::cores::mra {

struct Rom {
    int index = 0;
    std::string zips;
    std::string md5;
    std::uint32_t address = 0;
    bool closed = false;
    std::vector<RomNode> nodes;
};

}
