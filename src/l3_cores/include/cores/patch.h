// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>

#include "cores/types.h"

namespace mister::cores::mra {

struct Patch {
    std::string text;

    std::optional<RomOffset> address = RomOffset{};
    bool xor_op = false;
};

}
