// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "cores/ide/ide_drive_pair.h"
#include "cores/ide/ide_hot_swap.h"
#include "infra/seat.h"
#include "proto/disk_address.h"
#include "proto/types.h"

namespace mister::cores::ide {

enum class MountRule : std::uint8_t { DiskOnly, DiscElseAbsent };

using HotSwapRow = DrivePair<std::optional<HotSwapBit>>;

struct IdeChannelRow {
    TASTY_SEAT_EXEMPT(const_shared);

    proto::DiskAddress base{};

    proto::ReplyMask request{};

    proto::ReplyIndex task_file_at{};

    DrivePair<proto::DiskUnit> units{};
    MountRule mounts = MountRule::DiskOnly;
    HotSwapRow hot_swap{};
};

}
