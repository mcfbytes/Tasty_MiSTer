// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "hal/fpga_aperture.h"
#include "hal/phys_region.h"

namespace mister::hal {

struct FabricRegion {
    std::uint32_t offset;
    std::uint32_t len;
    const char* name;
};

[[nodiscard]] constexpr bool fabric_row_fits(
    const FabricRegion& r,
    const FpgaAperture& ap
) noexcept {
    if (ap.region.len == 0)
        return true;
    if (ap.region.len > std::numeric_limits<std::uint32_t>::max())
        return false;
    const auto extent = static_cast<std::uint32_t>(ap.region.len);
    if (r.len == 0)
        return r.offset < extent;
    return span_fits(r.offset, r.len, extent);
}

[[nodiscard]] constexpr bool fabric_rows_well_formed(
    std::span<const FabricRegion> rows,
    const FpgaAperture& ap
) noexcept {
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].name == nullptr || *rows[i].name == '\0')
            return false;
        if (!fabric_row_fits(rows[i], ap))
            return false;
        for (std::size_t j = i + 1; j < rows.size(); ++j) {
            if (rows[j].name == nullptr)
                return false;
            if (std::string_view(rows[i].name) == std::string_view(rows[j].name))
                return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr Ex<ApertureRegion> FpgaAperture::region_of(const FabricRegion& row) const {
    const auto at = translate(row.offset);
    if (!at)
        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), 0});
    if (!fabric_row_fits(row, *this)) {
        return std::unexpected(at->refusal(Errc::aperture_range, ERR_SITE()));
    }
    return ApertureRegion{PhysRegion{*at, row.len, row.name}};
}

}
