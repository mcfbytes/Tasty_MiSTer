// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/indexed_array.h"
#include "svc/track.h"
#include "svc/types.h"

namespace mister::svc {

enum class OffsetUnit : std::uint8_t { CueBytes, ChdSectors };

struct Toc {
    TASTY_SEAT_MEDIATOR(Any, Any);
    Track tracks[kMaxTracks]{};

    TrackCount last{};
    Lba end{};
    std::uint32_t sector_size = kCdDataSize;
    OffsetUnit offset_unit = OffsetUnit::CueBytes;
    bool has_subcode = false;

    [[nodiscard]] constexpr Track& at(TrackIndex index) noexcept {
        return infra::at_index(std::span<Track, kMaxTracks>(tracks), index);
    }

    [[nodiscard]] constexpr const Track& at(TrackIndex index) const noexcept {
        return infra::at_index(std::span<const Track, kMaxTracks>(tracks), index);
    }
};

}
