// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/error.h"
#include "os/mmio_region.h"
#include "hal/phys_region.h"
#include "infra/seat.h"

namespace mister::hal {

class FpgaMemory {
    TASTY_SEAT_EXEMPT(component);

public:
    static Ex<FpgaMemory> map(PhysRegion r,
                              os::MmioRegion::Access access = os::MmioRegion::Access::ReadWrite);

    static Ex<FpgaMemory> map_uio(
        int fd, os::PhysAddr node_base, PhysRegion r,
        os::MmioRegion::Access access = os::MmioRegion::Access::ReadWrite);

    static FpgaMemory borrow(std::span<std::byte> backing, PhysRegion r);

    FpgaMemory(FpgaMemory&& o) noexcept;
    FpgaMemory& operator=(FpgaMemory&& o) noexcept;
    FpgaMemory(const FpgaMemory&) = delete;
    FpgaMemory& operator=(const FpgaMemory&) = delete;
    ~FpgaMemory() = default;

    std::span<std::byte> view(std::size_t off, std::size_t len);

    Ex<std::size_t> write_at(std::size_t off, std::span<const std::byte> src);
    Ex<std::size_t> read_at(std::size_t off, std::span<std::byte> dst) const;

    void publish() noexcept;

    void handoff() noexcept;

    Ex<void> rebind(PhysRegion r);
    Ex<void> rebind(std::span<std::byte> backing, PhysRegion r);

    Ex<void> rebind_uio(int fd, os::PhysAddr node_base, PhysRegion r);

    bool uio_bound() const noexcept { return uio_bound_; }

    os::MmioRegion::Access access() const noexcept { return access_; }

    const PhysRegion& region() const noexcept { return region_; }

    std::uint32_t generation() const noexcept { return generation_; }

    void store_release(std::size_t word_off, std::uint32_t v);
    std::uint32_t load_acquire(std::size_t word_off) const;

private:
    FpgaMemory() = default;
    PhysRegion region_{};

    std::optional<os::MmioRegion> map_;
    std::byte* base_ = nullptr;
    std::uint32_t generation_ = 0;
    bool uio_bound_ = false;
    os::MmioRegion::Access access_ = os::MmioRegion::Access::ReadWrite;
};

}  // namespace mister::hal
