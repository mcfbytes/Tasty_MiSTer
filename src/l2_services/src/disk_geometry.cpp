// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disk_geometry.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <optional>
#include <span>

#include "svc/disk_image.h"
#include "svc/image_field.h"

namespace mister::svc {

namespace {

using Cylinders = DiskGeometry::Cylinders;
using Heads = DiskGeometry::Heads;
using Sectors = DiskGeometry::Sectors;

constexpr std::array<Sectors, 3> kTrackLadder{Sectors{63}, Sectors{127}, Sectors{255}};
constexpr Heads kFewestHeads{4};
constexpr Heads kMostHeads{16};

constexpr proto::BlockCount kSmallDiskBlocks{1024u * 1024u};
constexpr Cylinders kMostSmallCylinders{1023};
constexpr Cylinders kMostLargeCylinders{65536};

constexpr std::array<std::byte, 4>
    kRdbTag{std::byte{'R'}, std::byte{'D'}, std::byte{'S'}, std::byte{'K'}};
constexpr proto::FileOffset kRdbCylindersAt{64};
constexpr proto::FileOffset kRdbSectorsAt{68};
constexpr proto::FileOffset kRdbHeadsAt{72};

constexpr proto::Lba kRdbScanEnd{16};

constexpr Sectors kMostHeaderSectors{255};
constexpr Heads kMostHeaderHeads{16};
constexpr Sectors kTranslatableSectorsEnd{512};
constexpr Heads kMostTranslatableHeads{8};

using SectorBytes = std::array<std::byte, DiskGeometry::kSector.block_bytes()>;

[[nodiscard]] constexpr bool accepted(proto::BlockCount total, Cylinders cylinders) noexcept {
    if (total <= kSmallDiskBlocks)
        return cylinders <= kMostSmallCylinders;
    return cylinders <= kMostLargeCylinders;
}

[[nodiscard]] bool tagged_rdb(std::span<const std::byte> block) noexcept {
    return std::ranges::equal(block.first(kRdbTag.size()), kRdbTag);
}

[[nodiscard]] bool blank(std::span<const std::byte> block) noexcept {
    return std::ranges::all_of(block, [](std::byte b) { return b == std::byte{}; });
}

[[nodiscard]] DiskGeometry rdb_geometry(
    std::span<const std::byte> block,
    proto::FileSize size
) noexcept {
    constexpr ImageField<Cylinders, std::uint32_t, std::endian::big> kCylindersField{
        kRdbCylindersAt
    };
    constexpr ImageField<Heads, std::uint32_t, std::endian::big> kHeadsField{kRdbHeadsAt};
    constexpr ImageField<Sectors, std::uint32_t, std::endian::big> kSectorsField{kRdbSectorsAt};
    const DiskGeometry header{
        kCylindersField.read(block),
        kHeadsField.read(block),
        kSectorsField.read(block)
    };
    if (header.sectors <= kMostHeaderSectors && header.heads <= kMostHeaderHeads)
        return header;
    if (header.sectors.even()
        && header.sectors < kTranslatableSectorsEnd
        && header.heads <= kMostTranslatableHeads) {
        return DiskGeometry{header.cylinders, header.heads.doubled(), header.sectors.halved()};
    }
    return calc_disk_geometry(size);
}

}

DiskGeometry calc_disk_geometry(proto::FileSize size) noexcept {
    const proto::BlockCount total = DiskGeometry::kSector.blocks_in(size);
    Cylinders cylinders{};
    for (const Sectors sectors : kTrackLadder) {
        for (Heads heads = kFewestHeads; heads <= kMostHeads; heads = heads.next()) {
            cylinders = heads.cylinders_of(total, sectors);
            if (accepted(total, cylinders))
                return DiskGeometry{cylinders, heads, sectors};
        }
    }

    return DiskGeometry{cylinders, kMostHeads.next(), kTrackLadder.back()};
}

[[nodiscard]] Ex<DiskGeometry> guess_disk_geometry(IDiskImage& image, bool allow_virtual_rdb) {
    const proto::FileSize size = image.size();
    bool every_sector_blank = true;
    SectorBytes sector{};
    for (proto::Lba at{}; at < kRdbScanEnd; at += proto::BlockCount{1}) {
        const auto got = image.read_at(DiskGeometry::kSector.position_of(at), sector);
        if (!got || got->empty())
            break;
        if (!blank(sector))
            every_sector_blank = false;
        if (tagged_rdb(sector))
            return rdb_geometry(sector, size);
    }

    if (allow_virtual_rdb && !every_sector_blank)
        return unimplemented(ERR_SITE());
    return calc_disk_geometry(size);
}

}
