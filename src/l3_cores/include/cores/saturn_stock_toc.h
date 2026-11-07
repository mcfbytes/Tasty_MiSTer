// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/types.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "svc/disc_read_service.h"
#include "svc/track.h"

namespace mister::cores {

struct StockTrack {
    HeadLba start{};
    SectorCount index1{};
    friend constexpr bool operator==(StockTrack, StockTrack) = default;
};

class SaturnStockToc {
    TASTY_SEAT_RESIDENT(RT);

public:
    void learn(const svc::DiscGeometry& geom) noexcept;

    [[nodiscard]] HeadLba start_of(svc::TrackIndex track) const noexcept {
        return row(track).start;
    }

    [[nodiscard]] SectorCount index1_of(svc::TrackIndex track) const noexcept {
        return row(track).index1;
    }

private:
    [[nodiscard]] StockTrack row(svc::TrackIndex track) const noexcept {
        const auto r = rows_.find(track);
        return r ? *r : StockTrack{};
    }

    infra::IndexedArray<StockTrack, svc::kMaxTracks + 1, svc::TrackIndex> rows_{};
};

}
