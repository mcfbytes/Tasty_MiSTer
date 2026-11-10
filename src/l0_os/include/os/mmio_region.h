// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "os/map_length.h"
#include "os/types.h"
#include "infra/seat.h"

namespace mister::os {

class MmioRegion {

public:
    enum class Attr : std::uint8_t { Device, UncachedNormal };

    enum class Access : std::uint8_t { ReadWrite, ReadOnly };

    struct MapOffset {
        std::uint64_t v = 0;

        [[nodiscard]] static constexpr MapOffset of(PhysAddr phys) noexcept {
            return MapOffset{phys.v};
        }

        friend constexpr bool operator==(MapOffset, MapOffset) = default;

    private:
        friend class MmioRegion;

        [[nodiscard]] constexpr std::int64_t file_offset() const noexcept {
            return static_cast<std::int64_t>(v);
        }

        [[nodiscard]] constexpr std::uint32_t low32() const noexcept {
            return static_cast<std::uint32_t>(v);
        }
    };

    struct UioMapIndex {
        std::uint32_t v = 0;

        friend constexpr bool operator==(UioMapIndex, UioMapIndex) = default;

    private:
        friend class MmioRegion;

        [[nodiscard]] constexpr MapOffset offset_in(MapLength page) const noexcept {
            return MapOffset{static_cast<std::uint64_t>(v) * page.bytes()};
        }
    };

    [[nodiscard]] static Ex<MmioRegion> map(
        PhysAddr phys,
        std::size_t len,
        Attr attr,
        Access access = Access::ReadWrite
    );

    [[nodiscard]] static Ex<MmioRegion> map_fd(
        int fd,
        MapOffset offset,
        MapLength len,
        Access access = Access::ReadWrite
    );

    [[nodiscard]] static Ex<MmioRegion> map_uio(
        int fd,
        UioMapIndex index,
        MapLength len,
        Access access = Access::ReadWrite
    );

    Access access() const noexcept { return access_; }

    ~MmioRegion();
    MmioRegion(MmioRegion&&) noexcept;
    MmioRegion& operator=(MmioRegion&&) noexcept;
    MmioRegion(const MmioRegion&) = delete;
    MmioRegion& operator=(const MmioRegion&) = delete;

    volatile void* base() const noexcept { return base_; }

    [[nodiscard]] std::size_t length() const noexcept { return len_.bytes(); }

    [[nodiscard]] std::span<std::byte> plain_bytes() const noexcept {
        return len_.plain_bytes(const_cast<void*>(base_));
    }

private:
    MmioRegion() = default;

    [[nodiscard]] static Ex<MmioRegion> map_at(
        int fd,
        MapOffset offset,
        MapLength len,
        Access access
    );

    [[nodiscard]] static constexpr bool page_aligned(MapOffset off, MapLength granule) noexcept {
        return off.v % granule.bytes() == 0;
    }

    volatile void* base_ = nullptr;
    MapLength len_{};
    Access access_ = Access::ReadWrite;
};

}
