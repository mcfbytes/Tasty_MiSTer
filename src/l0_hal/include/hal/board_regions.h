// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <span>

#include "hal/aperture_region.h"
#include "hal/axi.h"
#include "hal/hps_ddr_region.h"
#include "hal/phys_region.h"
#include "hal/region_id.h"

namespace mister::hal {

class BoardRegions {
public:
    [[nodiscard]] static constexpr std::optional<ApertureRegion> aperture_region(
        std::span<const PhysRegion> rows,
        RegionId id
    ) noexcept {
        const Row found = sourced_row(rows, id);
        if (found.row == nullptr || found.side != ApertureSide::Inside)
            return std::nullopt;
        return ApertureRegion{*found.row};
    }

    [[nodiscard]] static constexpr std::optional<HpsDdrRegion> hps_region(
        std::span<const PhysRegion> rows,
        RegionId id
    ) noexcept {
        const Row found = sourced_row(rows, id);
        if (found.row == nullptr || found.side != ApertureSide::Below)
            return std::nullopt;
        return HpsDdrRegion{*found.row};
    }

private:
    struct Row {
        const PhysRegion* row;
        ApertureSide side;
    };

    [[nodiscard]] static constexpr Row sourced_row(
        std::span<const PhysRegion> r,
        RegionId id
    ) noexcept {
        if (r.size() != kRegionCount)
            return Row{nullptr, ApertureSide::Inside};
        const PhysRegion& row = row_of(r, id);
        if (row.len == regions::kLenUnsourced)
            return Row{nullptr, ApertureSide::Inside};
        return Row{&row, row_of(regions::kSide, id)};
    }
};

}
