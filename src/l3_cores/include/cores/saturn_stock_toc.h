// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "svc/disc_read_service.h"
#include "svc/track.h"

namespace mister::cores {

class SaturnStockToc {
    TASTY_SEAT_RESIDENT(RT);

public:
    void learn(const svc::DiscGeometry& geom) noexcept;

    [[nodiscard]] std::int32_t start_of(std::int32_t track) const noexcept {
        if (track < 0 || track > static_cast<std::int32_t>(svc::kMaxTracks))
            return 0;
        return start_[track];
    }

    [[nodiscard]] std::int32_t index1_of(std::int32_t track) const noexcept {
        return index1_[track];
    }

private:
    std::int32_t start_[svc::kMaxTracks + 1] = {};
    std::int32_t index1_[svc::kMaxTracks + 1] = {};
};

}
