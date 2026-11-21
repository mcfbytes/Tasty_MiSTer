// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/indexed_array.h"
#include "svc/disc/track.h"
#include "svc/types.h"

namespace mister::svc::disc {

struct Toc {
    TASTY_SEAT_MEDIATOR(Any, Any);

    infra::IndexedArray<Track, kMaxTracks, TrackIndex> tracks{};

    TrackCount last{};
    Lba end{};
    std::uint32_t sector_size = kRawFrameBytes;
    bool has_subcode = false;

    constexpr void close(TrackCount count) noexcept {
        last = count;
        Track& lead_out = tracks[count.next_index()];
        lead_out.number = count.next_index().number();
        lead_out.type = TrackType::Cdda;
        lead_out.start = end;
        lead_out.end = end;
    }
};

}
