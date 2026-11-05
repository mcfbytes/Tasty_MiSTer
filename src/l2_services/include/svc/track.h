// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "proto/types.h"
#include "svc/types.h"

namespace mister::svc {

using proto::BlockCount;
using proto::Lba;

inline constexpr std::size_t kMaxTracks = 100;

inline constexpr proto::BlockCount kRedBookPregap{150};
inline constexpr std::size_t kCdFrameSize = 2448;
inline constexpr std::size_t kCdDataSize = 2352;
inline constexpr std::uint32_t kChdTrackPadding = 4;

enum class TrackType : std::uint8_t { Cdda = 0, Mode1 = 1, Mode2 = 2 };

struct Track {
    TrackNumber number{};
    TrackType type = TrackType::Cdda;
    Lba start{};
    Lba end{};
    BlockCount pregap{};

    std::uint32_t sector_size = kCdDataSize;
    std::int64_t offset = 0;
    std::uint32_t index_num = 0;
    OpenFileId backing{};

    bool pregap_declared = false;
};

}
