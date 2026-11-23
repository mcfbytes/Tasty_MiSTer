// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/affine.h"

namespace mister::proto {

struct DiskRegisterCount {
    std::uint32_t v = 0;
    using affine_count = DiskRegisterCount;
    friend constexpr bool operator==(DiskRegisterCount, DiskRegisterCount) = default;
    friend constexpr auto operator<=>(DiskRegisterCount, DiskRegisterCount) = default;
};

}
