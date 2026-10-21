// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

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

[[nodiscard]] constexpr std::uint32_t flat_offset(
    std::uint32_t track,
    std::uint32_t head,
    std::uint32_t block,
    std::uint32_t sector
) noexcept {
    if (sector >= 90)
        sector -= 90;
    if (sector > 84)
        sector = 84;
    return (head != 0 ? kHeadStride : 0u)
        + track * kTrackStride
        + block * kBlockStride
        + sector * kSectorStride;
}

static_assert(flat_offset(kTracksPerHead - 1, 1, 1, 0) + kFlatBlockSpan <= kFlatBytes);
static_assert(kZone0BlockBytes == 0x4D08);

}
