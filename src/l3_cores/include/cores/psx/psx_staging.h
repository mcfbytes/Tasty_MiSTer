// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "cores/file_slot.h"
#include "cores/staging_policy.h"
#include "proto/types.h"

namespace mister::cores::psx {

inline constexpr auto kPsxSlots = std::to_array<FileSlot>({
    {.index = IoIndex{1},
     .extensions = "CUECHD",
     .label = "Load CD",
     .writable = false,
     .required = false,
     .role = SlotRole::Disc},
    {.index = IoIndex{2},
     .extensions = "SAVMCD",
     .label = "Mount Memory Card 1",
     .writable = true,
     .required = false,
     .role = SlotRole::Save},
    {.index = IoIndex{3},
     .extensions = "SAVMCD",
     .label = "Mount Memory Card 2",
     .writable = true,
     .required = false,
     .role = SlotRole::Save},
});

inline constexpr StagingPolicy kPsxStaging{
    .staged = true,
    .reset_pulse_us = 0,
    .disc_slot = disc_slot_of(kPsxSlots),
    .save = SaveMountPolicy{
        .slot = IoIndex{2},
        .pre_bytes = proto::FileSize{128 * 1024},

    },
};

}
