// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/core_profile.h"

namespace mister::cores {

struct CdSectorAct {
    IoIndex sub_index{};
    std::span<const std::uint8_t> subcode{};
    IoIndex index{};
    std::span<const std::uint8_t> sector{};
};

}
