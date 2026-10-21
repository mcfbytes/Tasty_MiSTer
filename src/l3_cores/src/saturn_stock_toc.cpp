// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/saturn_stock_toc.h"

namespace mister::cores {

void SaturnStockToc::learn(const svc::DiscGeometry& geom) noexcept {
    const svc::Toc& toc = geom.toc;
    const bool chd = geom.hunk_bytes != 0;
    std::int32_t file_end = 0;
    for (std::size_t i = 0; i <= svc::kMaxTracks; ++i) {
        start_[i] = 0;
        index1_[i] = 0;
    }
    for (std::uint32_t i = 0; i < toc.last && i < svc::kMaxTracks; ++i) {
        const svc::Track& t = toc.tracks[i];
        const auto start = static_cast<std::int32_t>(t.start.v);
        const bool own_file = i == 0 || t.backing != toc.tracks[i - 1].backing;
        if (chd || !own_file) {
            start_[i] = start;
            continue;
        }
        start_[i] = file_end;
        index1_[i] = start - file_end;
        file_end = static_cast<std::int32_t>(t.end.v);
    }
    if (toc.last <= svc::kMaxTracks)
        start_[toc.last] = static_cast<std::int32_t>(toc.end.v);
}

}
