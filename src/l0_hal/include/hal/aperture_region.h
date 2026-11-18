// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "hal/phys_region.h"

namespace mister::hal {

struct FpgaAperture;

class ApertureRegion {
public:
    [[nodiscard]] constexpr const PhysRegion& region() const noexcept { return r_; }

    [[nodiscard]] constexpr std::optional<ApertureRegion> sub(PhysRegion window) const noexcept {
        if (window.len == 0
            || !span_fits<std::uint64_t>(window.phys.offset_from(r_.phys), window.len, r_.len)) {
            return std::nullopt;
        }
        return ApertureRegion{window};
    }

private:
    friend struct FpgaAperture;
    friend class BoardRegions;

    constexpr explicit ApertureRegion(PhysRegion r) noexcept : r_(r) {}

    PhysRegion r_;
};

}
