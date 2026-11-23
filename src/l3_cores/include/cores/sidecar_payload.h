// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "proto/types.h"

namespace mister::cores {

struct SidecarPayload {
    proto::WideIoIndex dest{};
    std::vector<std::uint8_t> bytes{};
};

}
