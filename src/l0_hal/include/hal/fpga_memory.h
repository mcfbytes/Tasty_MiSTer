// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "os/mmio_region.h"
#include "hal/phys_region.h"
#include "infra/seat.h"

namespace mister::hal {

inline constexpr std::string_view kApertureUioName = "mister_ddr_aperture";

struct ApertureChoice {
    bool via_uio = false;
    int node = -1;
    os::PhysAddr node_base{};
    std::size_t node_size = 0;
};

[[nodiscard]] ApertureChoice resolve_aperture(const char* sysfs_root, const PhysRegion& r) noexcept;

class FpgaMemory {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static Ex<FpgaMemory> map(
        PhysRegion r, os::MmioRegion::Access access = os::MmioRegion::Access::ReadWrite);

    [[nodiscard]] static Ex<FpgaMemory> map_uio(
        int fd, os::PhysAddr node_base, PhysRegion r,
        os::MmioRegion::Access access = os::MmioRegion::Access::ReadWrite);

    [[nodiscard]] static Ex<FpgaMemory> map_aperture(
        PhysRegion r, os::MmioRegion::Access access = os::MmioRegion::Access::ReadWrite);

    static FpgaMemory borrow(std::span<std::byte> backing, PhysRegion r);

    FpgaMemory(FpgaMemory&& o) noexcept;
    FpgaMemory& operator=(FpgaMemory&& o) noexcept;
    FpgaMemory(const FpgaMemory&) = delete;
    FpgaMemory& operator=(const FpgaMemory&) = delete;
    ~FpgaMemory() = default;

    std::span<std::byte> view(std::size_t off, std::size_t len);

    [[nodiscard]] Ex<std::size_t> write_at(std::size_t off, std::span<const std::byte> src);
    [[nodiscard]] Ex<std::size_t> read_at(std::size_t off, std::span<std::byte> dst) const;

    void publish() noexcept;

    void handoff() noexcept;

    [[nodiscard]] Ex<void> rebind(PhysRegion r);
    [[nodiscard]] Ex<void> rebind(std::span<std::byte> backing, PhysRegion r);

    [[nodiscard]] Ex<void> rebind_uio(int fd, os::PhysAddr node_base, PhysRegion r);

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

void copy_to_uncached(std::byte* dst, const std::byte* src, std::size_t n) noexcept;

}  // namespace mister::hal
