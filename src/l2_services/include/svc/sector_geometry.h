// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/affine.h"
#include "svc/sector_types.h"

namespace mister::svc {

struct SectorGeometry {
    SectorSize size{};
    SectorCount count{};

    SectorCount short_lead{};

    static constexpr SectorSize kShortLead{128};

    [[nodiscard]] constexpr bool covers(SectorIndex at) const noexcept {
        return infra::affine::in_bounds(count, at);
    }

    [[nodiscard]] constexpr SectorSize size_of(SectorIndex at) const noexcept {
        return infra::affine::in_bounds(short_lead, at) ? kShortLead : size;
    }

    friend constexpr bool operator==(const SectorGeometry&, const SectorGeometry&) = default;
};

}
