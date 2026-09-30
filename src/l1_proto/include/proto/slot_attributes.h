// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::proto {

struct SlotAttributes {
    bool writable = false;
    bool growable = false;
    bool deferred_create = false;

    std::uint64_t channel_bytes = 0;
};

}  // namespace mister::proto
