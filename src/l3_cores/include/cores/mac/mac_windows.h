// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "cores/lba_window_row.h"
#include "cores/mac/blue_scsi_wire.h"
#include "proto/block_geometry.h"
#include "proto/types.h"
#include "svc/disc/track.h"

namespace mister::cores::mac {

inline constexpr proto::SlotIndex kToolboxSlot{3};
inline constexpr proto::SlotIndex kCdSlot{4};
inline constexpr proto::SlotIndex kChangerSlot{5};

inline constexpr proto::Lba kCdAudioLba{0x40000000u};
inline constexpr proto::Lba kCdTocLba{0x7FFF0000u};

inline constexpr LbaWindowRow kCdData{
    .slot = kCdSlot,
    .first = proto::Lba{},
    .blocks = kCdAudioLba - proto::Lba{},
    .shape = WindowShape::Data,
    .policy = proto::WindowPolicy::Image
};

inline constexpr LbaWindowRow kCdAudio{
    .slot = kCdSlot,
    .first = kCdAudioLba,
    .blocks = kCdTocLba - kCdAudioLba,
    .block_size = proto::BlockSize{svc::disc::kRawFrameBytes},
    .shape = WindowShape::Frame,
    .policy = proto::WindowPolicy::Extentless
};
inline constexpr LbaWindowRow kCdToc{
    .slot = kCdSlot,
    .first = kCdTocLba,
    .blocks = proto::BlockCount{2},
    .shape = WindowShape::Blob,
    .policy = proto::WindowPolicy::Extentless
};

inline constexpr proto::BlockCount kRpcBlocks{1 + kBlueScsiReplyBlocks};
inline constexpr LbaWindowRow kToolboxRpc{
    .slot = kToolboxSlot,
    .first = proto::Lba{},
    .blocks = kRpcBlocks,
    .shape = WindowShape::Rpc,
    .policy = proto::WindowPolicy::Extentless
};
inline constexpr LbaWindowRow kChangerRpc{
    .slot = kChangerSlot,
    .first = proto::Lba{},
    .blocks = kRpcBlocks,
    .shape = WindowShape::Rpc,
    .policy = proto::WindowPolicy::Extentless
};

inline constexpr auto kMacWindows = std::to_array<LbaWindowRow>(
    {kToolboxRpc, kCdData, kCdAudio, kCdToc, kChangerRpc}
);
static_assert(lba_windows_joined(kMacWindows));

}
