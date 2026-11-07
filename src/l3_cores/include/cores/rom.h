// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "cores/rom_node.h"
#include "hal/types.h"

namespace mister::cores::mra {

struct Rom {
    int index = 0;
    std::string zips;
    std::string md5;
    hal::FabricAddr address{};
    bool closed = false;
    std::vector<RomNode> nodes;
};

}
