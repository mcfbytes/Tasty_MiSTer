// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/aperture_region.h"
#include "hal/phys_region.h"
#include "infra/error.h"
#include "os/types.h"

namespace mister::hal {

struct FabricRegion;

struct FpgaAperture {
    PhysRegion region{};
    std::uint32_t mask = 0;

    [[nodiscard]] constexpr std::optional<os::PhysAddr> translate(
        std::uint32_t off
    ) const noexcept {
        if (region.len == 0)
            return std::nullopt;
        return region.phys.folded(off, mask);
    }

    [[nodiscard]] constexpr Ex<ApertureRegion> region_of(const FabricRegion& row) const;

    [[nodiscard]] constexpr Ex<ApertureRegion> resolve(
        os::PhysAddr addr,
        std::uint64_t len,
        const char* name = "load-window"
    ) const {
        if (region.len == 0)
            return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), 0});
        if (len == 0)
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
        const std::uint64_t extent = region.len;
        if (addr < region.phys || !span_fits(addr.offset_from(region.phys), len, extent)) {
            return std::unexpected(addr.refusal(Errc::aperture_range, ERR_SITE()));
        }
        return ApertureRegion{PhysRegion{addr, static_cast<std::size_t>(len), name}};
    }
};

constexpr std::optional<os::PhysAddr> FabricAddr::physical_in(
    const FpgaAperture& aperture
) const noexcept {
    const std::optional<FabricAddr> low = FabricAddr::of(aperture.region.phys);
    const std::optional<FabricAddr> high = FabricAddr::of(aperture.region.end());
    if (!low || !high || *this < *low || *this >= *high)
        return std::nullopt;
    return os::PhysAddr{v};
}

}
