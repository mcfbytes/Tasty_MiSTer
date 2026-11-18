// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "os/framebuffer.h"
#include "os/mmio_region.h"
#include "hal/aperture_region.h"
#include "hal/fbdev_placement.h"
#include "hal/hps_ddr_region.h"
#include "hal/memory_model.h"
#include "hal/phys_region.h"
#include "hal/types.h"
#include "infra/seat.h"

namespace mister::hal {

inline constexpr std::string_view kApertureUioName = "mister_ddr_aperture";

struct ApertureNode {
    os::UioMinor minor{};
    os::PhysAddr base{};
    std::size_t size = 0;
};

[[nodiscard]] std::optional<ApertureNode> resolve_aperture(
    const char* sysfs_root,
    const PhysRegion& r
) noexcept;

class FpgaMemory {
    TASTY_SEAT_EXEMPT(component);

public:
    enum class Mapping : std::uint8_t { DevMem, Uio, Fbdev };

    [[nodiscard]] static Ex<FpgaMemory> map(
        const ApertureRegion& r,
        os::MmioRegion::Access access = os::MmioRegion::Access::ReadWrite
    );
    [[nodiscard]] static Ex<FpgaMemory> map(
        const HpsDdrRegion& r,
        os::MmioRegion::Access access = os::MmioRegion::Access::ReadWrite
    );

    [[nodiscard]] static Ex<FpgaMemory> map_uio(
        int fd,
        os::PhysAddr node_base,
        const ApertureRegion& r,
        os::MmioRegion::Access access = os::MmioRegion::Access::ReadWrite
    );

    [[nodiscard]] static Ex<FpgaMemory> map_aperture(
        const ApertureRegion& r,
        os::MmioRegion::Access access = os::MmioRegion::Access::ReadWrite
    );

    [[nodiscard]] static Ex<FpgaMemory> map_scanout(
        const ApertureRegion& r,
        WriteCombine wc,
        const char* fb_node
    );

    [[nodiscard]] static Ex<FpgaMemory> map_fbdev(os::Framebuffer fb, const FbdevPlacement& at);

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

    [[nodiscard]] bool scanout_handoff() noexcept;

    bool uio_bound() const noexcept { return mapping_ == Mapping::Uio; }

    Mapping mapping() const noexcept { return mapping_; }

    os::MmioRegion::Access access() const noexcept { return access_; }

    const PhysRegion& region() const noexcept { return region_; }

    void store_release(WordOffset at, std::uint32_t v);
    std::uint32_t load_acquire(WordOffset at) const;

private:
    [[nodiscard]] static Ex<FpgaMemory> map_phys(PhysRegion r, os::MmioRegion::Access access);

    FpgaMemory() = default;
    PhysRegion region_{};

    std::optional<os::MmioRegion> map_;
    std::byte* base_ = nullptr;

    os::MapLength lead_{};
    Mapping mapping_ = Mapping::DevMem;
    os::MmioRegion::Access access_ = os::MmioRegion::Access::ReadWrite;

    std::optional<os::Framebuffer> fb_;
};

[[nodiscard]] constexpr const char* mapping_name(FpgaMemory::Mapping m) noexcept {
    switch (m) {
        case FpgaMemory::Mapping::DevMem:
            return "dev-mem";
        case FpgaMemory::Mapping::Uio:
            return "uio";
        case FpgaMemory::Mapping::Fbdev:
            return "fbdev-wc";
    }
    return "?";
}

}
