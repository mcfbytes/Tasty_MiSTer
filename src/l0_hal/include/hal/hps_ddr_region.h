// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/fpga_aperture.h"
#include "hal/phys_region.h"

namespace mister::hal {

namespace detail {

void page_not_below_aperture();
}

class HpsDdrRegion {
public:
    [[nodiscard]] constexpr const PhysRegion& region() const noexcept { return r_; }

    [[nodiscard]] static consteval HpsDdrRegion below(const FpgaAperture& ap, PhysRegion r);

private:
    friend class BoardRegions;

    constexpr explicit HpsDdrRegion(PhysRegion r) noexcept : r_(r) {}

    PhysRegion r_;
};

consteval HpsDdrRegion HpsDdrRegion::below(const FpgaAperture& ap, PhysRegion r) {
    if (ap.region.len == 0 || r.len == 0 || r.end() > ap.region.phys) {
        detail::page_not_below_aperture();
    }
    return HpsDdrRegion{r};
}

}
