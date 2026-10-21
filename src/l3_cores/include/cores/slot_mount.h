// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "cores/staging_core.h"
#include "proto/types.h"

namespace mister::cores {

class ISlotMount {
public:
    virtual ~ISlotMount() = default;

    [[nodiscard]] virtual MountState mount_slot(proto::SlotIndex slot, std::string_view path) = 0;

protected:
    ISlotMount() = default;
    ISlotMount(const ISlotMount&) = default;
    ISlotMount& operator=(const ISlotMount&) = default;
};

}
