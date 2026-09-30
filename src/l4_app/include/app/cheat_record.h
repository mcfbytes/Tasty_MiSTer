// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "infra/fixed_str.h"

namespace mister::app {

struct CheatRecord {
    static constexpr std::size_t kNameCap = 256;

    FixedStr<kNameCap, StrFit::Clip> name{};
    std::vector<std::uint8_t> bytes;
    bool enabled = false;
    bool loaded = false;
};

}  // namespace mister::app
