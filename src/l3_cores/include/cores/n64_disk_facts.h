// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "cores/n64_load_facts.h"

namespace mister::cores::n64 {

struct DiskFacts {
    std::uint32_t load_addr = 0;
    std::uint32_t pad = 0;
};

static_assert(std::is_trivially_copyable_v<DiskFacts> && sizeof(DiskFacts) != sizeof(LoadFacts));

}
