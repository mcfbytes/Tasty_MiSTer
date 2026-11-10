// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/psx_sector_map.h"

namespace mister::cores {

PsxSectorMap::Answer PsxSectorMap::classify(proto::Lba lba) const noexcept {
    const svc::Toc* toc = disc_.mounted ? &disc_.toc : nullptr;
    if (toc == nullptr
        || toc->last == svc::TrackCount{}
        || lba < toc->at(svc::TrackIndex{}).start) {
        return Answer::Zeros;
    }
    const auto ti = svc::track_for_lba(*toc, lba);
    if (!ti.has_value())
        return Answer::Filler;
    const svc::TrackIndex next_index = *ti + svc::TrackCount{1};
    if (toc->last.contains(next_index)) {
        const svc::Track& next = toc->at(next_index);

        if (next.pregap_declared && lba > next.start - next.pregap)
            return Answer::Pregap;
    }
    return Answer::Engine;
}

}
