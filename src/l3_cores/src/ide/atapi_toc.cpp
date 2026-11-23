// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/ide/atapi_toc.h"

namespace mister::cores::ide {

using svc::TrackCount;
using svc::TrackIndex;
using svc::disc::Track;

std::optional<AtapiToc::Bounds> AtapiToc::bounds() const noexcept {
    if (toc_.last.empty())
        return std::nullopt;
    const Track& first = toc_.tracks[TrackIndex{}];
    if (first.end == first.start)
        return std::nullopt;
    return Bounds{
        .first = first.number,
        .last = toc_.tracks[toc_.last.last_index()].number,
        .lead_out = toc_.end
    };
}

std::optional<AtapiToc::Entry> AtapiToc::entry(svc::TrackNumber requested) const noexcept {
    if (requested == svc::TrackNumber{})
        return std::nullopt;
    const TrackIndex index = requested.index();
    if (!toc_.last.contains(index))
        return std::nullopt;
    const Track& track = toc_.tracks[index];
    return Entry{.start = track.start, .type = track.type};
}

std::optional<AtapiToc::Hit> AtapiToc::row_at(proto::Lba block) const noexcept {
    const TrackCount count = rows();
    proto::Lba from{};
    for (TrackIndex index{}; count.contains(index); index += TrackCount{1}) {
        const Track track = row(index);
        if (block >= from && block <= track.end)
            return Hit{.row = index, .pregap = block < track.start};
        from = track.end;
    }
    return std::nullopt;
}

Track AtapiToc::row(TrackIndex index) const noexcept {
    if (toc_.last.contains(index))
        return toc_.tracks[index];
    Track out{};
    if (index == toc_.last.next_index()) {
        out.number = index.number();
        out.start = toc_.end;
        out.end = toc_.end;
    }
    return out;
}

TrackCount AtapiToc::rows() const noexcept {
    return toc_.last + TrackCount{1};
}

}
