// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <compare>
#include <cstdint>
#include <limits>

#include "hal/types.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "proto/types.h"

namespace mister::svc {

class IDiskImage;

struct DiskGeometry {

    struct Sectors {
        std::uint32_t v = 0;

        [[nodiscard]] constexpr Sectors or_else(Sectors fallback) const noexcept {
            return v == 0 ? fallback : *this;
        }

        [[nodiscard]] constexpr Sectors halved() const noexcept { return Sectors{v >> 1}; }

        [[nodiscard]] constexpr bool even() const noexcept { return (v & 1u) == 0; }

        [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

        [[nodiscard]] constexpr Sectors in_byte() const noexcept {
            return Sectors{v & std::numeric_limits<std::uint8_t>::max()};
        }

        [[nodiscard]] constexpr proto::BlockCount track_blocks() const noexcept {
            return proto::BlockCount{v};
        }

        [[nodiscard]] constexpr hal::SpiWord word() const noexcept {
            return hal::SpiWord{static_cast<std::uint16_t>(v)};
        }

        friend constexpr bool operator==(Sectors, Sectors) = default;
        friend constexpr auto operator<=>(Sectors, Sectors) = default;
    };

    struct Cylinders {
        std::uint32_t v = 0;

        [[nodiscard]] constexpr Cylinders capped(Cylinders most) const noexcept {
            return std::min(*this, most);
        }

        [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

        [[nodiscard]] constexpr Cylinders next() const noexcept { return Cylinders{v + 1}; }

        [[nodiscard]] constexpr hal::SpiWord word() const noexcept {
            return hal::SpiWord{static_cast<std::uint16_t>(v)};
        }

        [[nodiscard]] constexpr proto::BlockCount blocks(proto::BlockCount each) const noexcept {
            return each * v;
        }

        friend constexpr bool operator==(Cylinders, Cylinders) = default;
        friend constexpr auto operator<=>(Cylinders, Cylinders) = default;
    };

    struct Heads {
        std::uint32_t v = 0;

        [[nodiscard]] constexpr Heads or_else(Heads fallback) const noexcept {
            return v == 0 ? fallback : *this;
        }

        [[nodiscard]] constexpr Heads doubled() const noexcept { return Heads{v << 1}; }

        [[nodiscard]] constexpr Heads next() const noexcept { return Heads{v + 1}; }

        [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

        [[nodiscard]] constexpr hal::SpiWord word() const noexcept {
            return hal::SpiWord{static_cast<std::uint16_t>(v)};
        }

        [[nodiscard]] constexpr proto::BlockCount cylinder_blocks(
            Sectors per_track
        ) const noexcept {
            return proto::BlockCount{v * per_track.v};
        }

        [[nodiscard]] constexpr Cylinders cylinders_of(
            proto::BlockCount total,
            Sectors per_track
        ) const noexcept {
            return Cylinders{total / cylinder_blocks(per_track)};
        }

        [[nodiscard]] constexpr Cylinders cylinders_in(
            proto::FileSize image,
            Sectors per_track
        ) const noexcept {
            const std::uint64_t cylinder_bytes = std::uint64_t{v} * per_track.v * kSector.v;
            return Cylinders{static_cast<std::uint32_t>(image.v / cylinder_bytes)}.capped(
                kMostAddressed
            );
        }

        friend constexpr bool operator==(Heads, Heads) = default;
        friend constexpr auto operator<=>(Heads, Heads) = default;
    };

    static constexpr proto::BlockSize kSector{512};

    static constexpr Heads kDefaultHeads{16};
    static constexpr Sectors kDefaultSectors{256};

    static constexpr Cylinders kMostAddressed{65535};

    Cylinders cylinders{};
    Heads heads{};
    Sectors sectors{};

    [[nodiscard]] constexpr DiskGeometry addressing(proto::FileSize image) const noexcept {
        const Heads h = heads.or_else(kDefaultHeads);
        const Sectors s = sectors.or_else(kDefaultSectors);
        return DiskGeometry{h.cylinders_in(image, s), h, s};
    }

    [[nodiscard]] constexpr proto::BlockCount cylinder_blocks() const noexcept {
        return heads.cylinder_blocks(sectors);
    }

    [[nodiscard]] constexpr proto::BlockCount blocks() const noexcept {
        return cylinders.blocks(cylinder_blocks());
    }

    [[nodiscard]] constexpr proto::FileSize bytes() const noexcept {
        return kSector.bytes_of(blocks());
    }

    friend constexpr bool operator==(const DiskGeometry&, const DiskGeometry&) = default;
};

[[nodiscard]] DiskGeometry calc_disk_geometry(proto::FileSize size) noexcept;

[[nodiscard]] Ex<DiskGeometry> guess_disk_geometry(IDiskImage& image, bool allow_virtual_rdb);

}
