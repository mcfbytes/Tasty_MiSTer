// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mister::cores::mra {

struct MraCheat {
    std::string name;
    std::vector<std::uint8_t> bytes;
};

}
