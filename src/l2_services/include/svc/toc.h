// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/track.h"

namespace mister::svc {

enum class OffsetUnit : std::uint8_t { CueBytes, ChdSectors };

struct Toc {
    Track tracks[kMaxTracks]{};
    std::uint8_t last = 0;
    Lba end{};
    std::uint32_t sector_size = kCdDataSize;
    OffsetUnit offset_unit = OffsetUnit::CueBytes;
    bool has_subcode = false;
};

}  // namespace mister::svc
