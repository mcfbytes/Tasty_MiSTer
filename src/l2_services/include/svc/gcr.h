// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include "infra/byte_count.h"
#include "proto/track_shape.h"
#include "proto/types.h"
#include "svc/gcr_caps.h"
#include "svc/gcr_layout.h"
#include "svc/gcr_sector_map.h"
#include "svc/gcr_types.h"

namespace mister::svc::gcr {

inline constexpr proto::TrackShape kTrackShape{.unit = SectorMap::kSector, .counts = {32, 53}};

inline constexpr std::byte kSync{0xFF};
inline constexpr std::byte kHeaderMark{0x08};
inline constexpr std::byte kDataMark{0x07};

enum Block : std::size_t {
    kSectorDataBytes = 256,
    kDataBlockBytes = 1 + kSectorDataBytes + 3,
    kHeaderBlockBytes = 8,
};

inline constexpr infra::ByteCount kSyncRun{5};
inline constexpr infra::ByteCount kHeaderGap{9};

inline constexpr infra::ByteCount kSectorStream = kSyncRun
    + kSyncRun
    + kHeaderGap
    + Group::coded(infra::ByteCount{kHeaderBlockBytes + kDataBlockBytes});

inline constexpr infra::ByteCount kEmptyTrack{4096};
inline constexpr infra::ByteCount kEmptyReply1571{12502};

enum Extent : std::size_t {

    kSectorBufferBytes = 8192,

    kHeaderBytes = 12,
    kSlotCountAt = 9,
    kMostSlots = 168,
};

static_assert(
    kSectorStream == infra::ByteCount{354}
        && kDataBlockBytes % Group::kDataBytes == 0
        && kHeaderBlockBytes % Group::kDataBytes == 0,
    "c64.cpp c64_synthesize_gcr_track: 5 + 10 + 9 + 5 + 325 bytes before the zone gap"
);

static_assert(
    kTablesAt == proto::kFileStart + proto::FileSize{kHeaderBytes},
    "c64.cpp openGCR: the position and speed tables follow the 12-byte header"
);

[[nodiscard]] infra::ByteCount synthesize_track(
    LogicalTrack track,
    const DiskId& id,
    std::span<const std::byte> sectors,
    std::span<std::byte> out
) noexcept;

[[nodiscard]] SpeedZone written_speed(
    TrackWord word,
    TrackTotal slots,
    TrackLength length
) noexcept;

[[nodiscard]] std::optional<TrackTotal> track_slots(std::span<const std::byte> head) noexcept;

[[nodiscard]] std::optional<GcrCaps> image_caps(
    GcrLayout layout,
    proto::FileSize size,
    std::span<const std::byte> head
) noexcept;

}
