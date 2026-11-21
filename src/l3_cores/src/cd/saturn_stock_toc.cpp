// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd/saturn_stock_toc.h"

namespace mister::cores::cd {

void SaturnStockToc::learn(const svc::disc::DiscGeometry& geom) noexcept {
    const svc::disc::Toc& toc = geom.toc;
    const bool chd = geom.chd_hunk_bytes() != 0;
    HeadLba file_end{};
    rows_ = {};
    toc.last.for_each_index([&](svc::TrackIndex i) {
        const svc::disc::Track& t = toc.tracks[i];
        const HeadLba start = HeadLba::of(t.start);
        const bool own_file = i == svc::TrackIndex{}
            || t.backing != toc.tracks[i - svc::TrackCount{1}].backing;
        if (chd || !own_file) {
            rows_[i].start = start;
            return;
        }
        rows_[i].start = file_end;
        rows_[i].index1 = start - file_end;
        file_end = HeadLba::of(t.end);
    });
    if (const auto last = rows_.find(toc.last.next_index()))
        last->start = HeadLba::of(toc.end);
}

}
