// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/indexed_array.h"
#include "svc/track.h"
#include "svc/types.h"

namespace mister::svc {

struct Toc {
    TASTY_SEAT_MEDIATOR(Any, Any);
    Track tracks[kMaxTracks]{};

    TrackCount last{};
    Lba end{};
    std::uint32_t sector_size = kRawFrameBytes;
    bool has_subcode = false;

    [[nodiscard]] constexpr Track& at(TrackIndex index) noexcept {
        return infra::at_index(std::span<Track, kMaxTracks>(tracks), index);
    }

    [[nodiscard]] constexpr const Track& at(TrackIndex index) const noexcept {
        return infra::at_index(std::span<const Track, kMaxTracks>(tracks), index);
    }

    constexpr void close(TrackCount count) noexcept {
        last = count;
        Track& lead_out = at(count.next_index());
        lead_out.number = count.next_index().number();
        lead_out.type = TrackType::Cdda;
        lead_out.start = end;
        lead_out.end = end;
    }
};

}
