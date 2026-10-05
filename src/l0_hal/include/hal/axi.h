// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "os/mmio_region.h"
#include "hal/phys_region.h"
#include "hal/region_id.h"
#include "hal/types.h"
#include "os/types.h"

namespace mister::hal {

namespace regions {

inline constexpr std::size_t kLenUnsourced = 0;

inline constexpr PhysRegion kVideoFb{os::PhysAddr{0x2200'0000u}, 0x0080'0000u, "video-fb"};

inline constexpr PhysRegion kScalerOut{os::PhysAddr{0x2000'0000u}, 0x0060'0000u, "scaler-out"};

inline constexpr PhysRegion kMinimigShare{os::PhysAddr{0x27FF'4000u}, 0x0000'2000u,
                                          "minimig-share"};

inline constexpr PhysRegion kX86Share{os::PhysAddr{0x300C'E000u}, 0x0000'2000u, "x86-share"};

inline constexpr PhysRegion kX86Mem{os::PhysAddr{0x3000'0000u}, kLenUnsourced, "x86-mem"};

inline constexpr PhysRegion kA2065Flat{os::PhysAddr{0x1FF0'0000u}, 0x0001'0000u, "a2065-flat"};

inline constexpr PhysRegion kSaturnCdBuf{os::PhysAddr{0x3100'0000u}, 0x0000'4000u, "saturn-cd-buf"};

inline constexpr std::array<PhysRegion, kRegionCount> kCatalog{
    kMinimigShare, kX86Share, kX86Mem, kA2065Flat, kSaturnCdBuf, kVideoFb, kScalerOut,
};

constexpr bool catalog_names_are_distinct() noexcept {
    for (std::size_t i = 0; i < kRegionCount; ++i) {
        if (kCatalog[i].name == nullptr || *kCatalog[i].name == '\0') return false;
        for (std::size_t j = i + 1; j < kRegionCount; ++j) {
            if (std::string_view(kCatalog[i].name) == std::string_view(kCatalog[j].name)) {
                return false;
            }
        }
    }
    return true;
}
static_assert(catalog_names_are_distinct(),
              "every regions:: catalog row needs a name, and no two may share one — a "
              "duplicate resolves the wrong row in a by-name window lookup");

inline constexpr std::array<PhysRegion, kRegionCount> kUnsourced = [] {
    std::array<PhysRegion, kRegionCount> r{};
    for (std::size_t i = 0; i < kRegionCount; ++i) {
        r[i] = PhysRegion{os::PhysAddr{0u}, kLenUnsourced, kCatalog[i].name};
    }
    return r;
}();

}  // namespace regions

constexpr bool regions_well_formed(std::span<const PhysRegion> r) noexcept {
    if (r.size() != kRegionCount) return false;
    for (std::size_t i = 0; i < kRegionCount; ++i) {
        if (std::string_view(r[i].name) != std::string_view(regions::kCatalog[i].name)) {
            return false;
        }
    }
    return true;
}

inline constexpr os::PhysAddr kLwBridgeBase{0xFF20'0000u};
inline constexpr std::size_t kLwBridgeSize = 0x0020'0000u;

inline constexpr os::PhysAddr kFpgaMemBase{0x2000'0000u};
inline constexpr std::uint32_t kFpgaMemMask = 0x1FFF'FFFFu;

constexpr os::PhysAddr fpga_mem(std::uint32_t addr) noexcept {
    return os::PhysAddr{kFpgaMemBase.v | (addr & kFpgaMemMask)};
}

namespace detail {

struct MappedRegs {
    os::MmioRegion pages;
    std::size_t lead = 0;
    bool via_uio = false;
};

Ex<MappedRegs> map_registers(os::PhysAddr phys, std::size_t len);

Ex<MappedRegs> map_lw_registers(PhysRegion lw, LwOffset off, std::size_t len);

[[nodiscard]] Ex<MappedRegs> map_named(std::string_view uio_name, os::PhysAddr phys,
                                       std::size_t len);

std::uint32_t regs_read(const volatile void* base, std::size_t byte_off);
void regs_write(volatile void* base, std::size_t byte_off, std::uint32_t v);

void regs_fence() noexcept;

inline volatile void* regs_advance(volatile void* p, std::size_t n) noexcept {
    return reinterpret_cast<volatile std::byte*>(p) + n;
}

}  // namespace detail

}  // namespace mister::hal
