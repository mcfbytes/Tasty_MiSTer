// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/core_window.h"
#include "infra/error.h"
#include "proto/types.h"

namespace mister::cores::n64dd {

inline constexpr std::uint32_t kNddBytes = 0x03DE'C800;
inline constexpr std::uint32_t kFlatBytes = 0x06E2'8000;
inline constexpr std::uint32_t kIplBytes = 4u * 1024u * 1024u;
inline constexpr std::uint32_t kIplMinBytes = 4096;
inline constexpr std::uint32_t kLbaCount = 0x10DC;
inline constexpr std::uint32_t kSectorsPerBlock = 85;
inline constexpr std::uint32_t kSystemSectorBytes = 232;
inline constexpr std::uint32_t kDevSystemSectorBytes = 192;
inline constexpr std::uint32_t kBadTracksPerZone = 12;
inline constexpr std::uint32_t kHeadStride = 0x0371'4000;
inline constexpr std::uint32_t kTrackStride = 0x0000'C000;
inline constexpr std::uint32_t kBlockStride = 0x0000'6000;
inline constexpr std::uint32_t kSectorStride = 0x0000'0100;
inline constexpr std::uint32_t kTracksPerHead = 1175;

inline constexpr std::uint32_t kFlatBlockSpan = kSectorsPerBlock * kSectorStride;
inline constexpr std::uint32_t kDirtyFlagOffset = 0x0000'5F00;
inline constexpr std::uint32_t kDirtyMarkerBytes = 8;
inline constexpr std::uint8_t kDirtyMagic = 0xD1;
inline constexpr std::uint8_t kBadBlockFill = 0xDD;
inline constexpr std::uint32_t kBadMarkOffset = 0xF8;
inline constexpr std::uint32_t kBadMarkBytes = 8;
inline constexpr std::uint32_t kDiskTypes = 7;
inline constexpr std::uint32_t kZones = 16;

inline constexpr std::array<std::uint8_t, 4> kDevSystemLbas{11, 10, 3, 2};

inline constexpr std::array<std::uint16_t, kDiskTypes> kRamStartLba =
    {0x05A2, 0x07C6, 0x09EA, 0x0C0E, 0x0E32, 0x1010, 0x10DC};
inline constexpr std::array<std::uint32_t, kDiskTypes> kRamBytes =
    {0x024A'9DC0, 0x01C2'26C0, 0x0145'0F00, 0x00D3'5680, 0x006C'FD40, 0x001D'A240, 0};

struct Zone {
    std::uint8_t head;
    std::uint16_t sector_bytes;
    std::uint16_t tracks;
    std::uint16_t track_offset;
};

inline constexpr std::array<Zone, kZones> kZoneTable = {{
    {0, 232, 158, 0},
    {0, 216, 158, 158},
    {0, 208, 149, 316},
    {0, 192, 149, 465},
    {0, 176, 149, 614},
    {0, 160, 149, 763},
    {0, 144, 149, 912},
    {0, 128, 114, 1061},
    {1, 216, 158, 0},
    {1, 208, 158, 158},
    {1, 192, 149, 316},
    {1, 176, 149, 465},
    {1, 160, 149, 614},
    {1, 144, 149, 763},
    {1, 128, 149, 912},
    {1, 112, 114, 1061},
}};

inline constexpr std::array<std::array<std::uint8_t, kZones>, kDiskTypes> kVzoneToPzone = {{
    {0, 1, 2, 9, 8, 3, 4, 5, 6, 7, 15, 14, 13, 12, 11, 10},
    {0, 1, 2, 3, 10, 9, 8, 4, 5, 6, 7, 15, 14, 13, 12, 11},
    {0, 1, 2, 3, 4, 11, 10, 9, 8, 5, 6, 7, 15, 14, 13, 12},
    {0, 1, 2, 3, 4, 5, 12, 11, 10, 9, 8, 6, 7, 15, 14, 13},
    {0, 1, 2, 3, 4, 5, 6, 13, 12, 11, 10, 9, 8, 7, 15, 14},
    {0, 1, 2, 3, 4, 5, 6, 7, 14, 13, 12, 11, 10, 9, 8, 15},
    {0, 1, 2, 3, 4, 5, 6, 7, 15, 14, 13, 12, 11, 10, 9, 8},
}};

inline constexpr std::uint32_t kZone0BlockBytes = 232u * kSectorsPerBlock;

struct FlatOffset {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr FlatOffset advanced_by(std::uint32_t bytes) const noexcept {
        return FlatOffset{v + bytes};
    }

    [[nodiscard]] constexpr std::uint32_t bytes() const noexcept { return v; }

    [[nodiscard]] constexpr bool fits(proto::FileSize span, proto::FileSize image) const noexcept {
        return proto::FileSize{v} + span <= image;
    }

    [[nodiscard]] Ex<void> write_via(ICoreWindow& window, std::span<const std::byte> bytes) const {
        auto w = window.write(v, bytes);
        if (!w)
            return std::unexpected(w.error());
        return {};
    }

    [[nodiscard]] Ex<void> read_via(ICoreWindow& window, std::span<std::byte> bytes) const {
        auto r = window.read(v, bytes);
        if (!r)
            return std::unexpected(r.error());
        return {};
    }

    friend constexpr bool operator==(FlatOffset, FlatOffset) = default;

    friend constexpr FlatOffset operator+(FlatOffset a, FlatOffset b) noexcept {
        return FlatOffset{a.v + b.v};
    }
};

struct DiskHead {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr FlatOffset stride() const noexcept {
        return FlatOffset{v != 0 ? kHeadStride : 0u};
    }

    friend constexpr bool operator==(DiskHead, DiskHead) = default;
};

struct DiskTrack {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr FlatOffset stride() const noexcept {
        return FlatOffset{std::uint32_t{v} * kTrackStride};
    }

    friend constexpr bool operator==(DiskTrack, DiskTrack) = default;
};

struct TrackBlock {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr FlatOffset stride() const noexcept {
        return FlatOffset{std::uint32_t{v} * kBlockStride};
    }

    friend constexpr bool operator==(TrackBlock, TrackBlock) = default;
};

struct FlatBlock {
    DiskTrack track{};
    DiskHead head{};
    TrackBlock block{};

    [[nodiscard]] constexpr FlatOffset offset() const noexcept {
        return head.stride() + track.stride() + block.stride();
    }

    [[nodiscard]] constexpr FlatOffset sector_offset(std::uint32_t sector) const noexcept {

        constexpr std::uint32_t kSectorNumbersPerBlock = 90;
        if (sector >= kSectorNumbersPerBlock)
            sector -= kSectorNumbersPerBlock;
        if (sector >= kSectorsPerBlock)
            sector = kSectorsPerBlock - 1;
        return offset().advanced_by(sector * kSectorStride);
    }

    [[nodiscard]] static constexpr FlatBlock of_index(std::uint32_t k) noexcept {
        constexpr std::uint32_t kBlocksPerTrack = 2;
        const std::uint16_t track = static_cast<std::uint16_t>(
            (k / kBlocksPerTrack) % kTracksPerHead
        );
        const std::uint8_t head = static_cast<std::uint8_t>(k / (kBlocksPerTrack * kTracksPerHead));
        return FlatBlock{
            DiskTrack{track},
            DiskHead{head},
            TrackBlock{static_cast<std::uint8_t>(k % kBlocksPerTrack)}
        };
    }

    friend constexpr bool operator==(FlatBlock, FlatBlock) = default;
};

inline constexpr FlatBlock kLastFlatBlock = FlatBlock::of_index(2u * kTracksPerHead * 2u - 1u);
static_assert(
    kLastFlatBlock.offset().fits(proto::FileSize{kFlatBlockSpan}, proto::FileSize{kFlatBytes})
);
static_assert(kZone0BlockBytes == 0x4D08);

}
