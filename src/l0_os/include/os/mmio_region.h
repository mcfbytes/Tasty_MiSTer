// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "os/types.h"
#include "infra/seat.h"

namespace mister::os {

class MmioRegion {
    TASTY_SEAT_EXEMPT(component);

public:
    enum class Attr : std::uint8_t { Device, UncachedNormal };

    enum class Access : std::uint8_t { ReadWrite, ReadOnly };

    [[nodiscard]] static Ex<MmioRegion> map(
        PhysAddr phys,
        std::size_t len,
        Attr attr,
        Access access = Access::ReadWrite
    );

    [[nodiscard]] static Ex<MmioRegion> map_fd(
        int fd,
        PhysAddr offset,
        std::size_t len,
        Access access = Access::ReadWrite
    );

    Access access() const noexcept { return access_; }

    ~MmioRegion();
    MmioRegion(MmioRegion&&) noexcept;
    MmioRegion& operator=(MmioRegion&&) noexcept;
    MmioRegion(const MmioRegion&) = delete;
    MmioRegion& operator=(const MmioRegion&) = delete;

    volatile void* base() const noexcept { return base_; }

    std::size_t length() const noexcept { return len_; }

    [[nodiscard]] std::span<std::byte> plain_bytes() const noexcept {
        return std::span<std::byte>(reinterpret_cast<std::byte*>(const_cast<void*>(base_)), len_);
    }

private:
    MmioRegion() = default;
    volatile void* base_ = nullptr;
    std::size_t len_ = 0;
    Access access_ = Access::ReadWrite;
};

}
