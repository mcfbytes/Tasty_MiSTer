// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/psx_sector_map.h"

namespace mister::cores {

PsxSectorMap::Answer PsxSectorMap::classify(std::int64_t lba) const noexcept {
    const svc::Toc* toc = disc_.mounted ? &disc_.toc : nullptr;
    if (toc == nullptr
        || toc->last == svc::TrackCount{}
        || lba < static_cast<std::int64_t>(toc->at(svc::TrackIndex{}).start.v)) {
        return Answer::Zeros;
    }
    const auto ti = svc::track_for_lba(*toc, proto::Lba{static_cast<std::uint32_t>(lba)});
    if (!ti.has_value())
        return Answer::Filler;
    const svc::TrackIndex next_index = *ti + svc::TrackCount{1};
    if (toc->last.contains(next_index)) {
        const svc::Track& next = toc->at(next_index);

        if (next.pregap_declared
            && lba > static_cast<std::int64_t>(next.start.v)
                    - static_cast<std::int64_t>(next.pregap.v)) {
            return Answer::Pregap;
        }
    }
    return Answer::Engine;
}

}
