// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"

namespace mister::proto {

struct SdRequest {
    SlotIndex slot;
    bool write;
    Lba lba;
    BlockCount block_count;
};

}  // namespace mister::proto
