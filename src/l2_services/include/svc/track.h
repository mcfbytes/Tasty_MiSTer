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

inline constexpr std::size_t kUserDataBytes = 2048;
inline constexpr std::size_t kRawFrameBytes = 2352;
inline constexpr std::size_t kSubcodeBytes = 96;
inline constexpr std::size_t kFullFrameBytes = 2448;
inline constexpr proto::BlockCount kChdTrackPadding{4};

struct SubcodeByte {
    std::uint8_t v = 0;

    enum : unsigned { kQChannel = 1, kChannelBits = 8 };

    friend constexpr bool operator==(SubcodeByte, SubcodeByte) = default;
};

enum class TrackType : std::uint8_t { Cdda = 0, Mode1 = 1, Mode2 = 2 };

struct Track {

    static constexpr std::uint32_t kIndexes = 2;

    TrackNumber number{};
    TrackType type = TrackType::Cdda;
    Lba start{};
    Lba end{};
    BlockCount pregap{};

    std::uint32_t sector_size = kRawFrameBytes;
    std::int64_t offset = 0;
    std::uint32_t index_num = 0;
    OpenFileId backing{};

    bool pregap_declared = false;
};

}
