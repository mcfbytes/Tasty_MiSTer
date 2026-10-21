// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"

namespace mister::cores::n64 {

inline constexpr proto::IoIndex kPifIndex{0};

inline constexpr proto::IoIndex kRomIndex{1};
inline constexpr proto::IoIndex kGbIndex{2};
inline constexpr proto::IoIndex kDdDiskIndex{3};
inline constexpr proto::IoIndex kDdIplIndex{4};

inline constexpr proto::IoIndex kDdIplPreloadIndex{5};

enum class FileRoute : std::uint8_t { Pif, Rom, GameBoy, DiskImage, Ipl };

[[nodiscard]] constexpr FileRoute file_route(proto::IoIndex index) noexcept {
    const std::uint8_t arm = index.v & 0x3fu;
    if (arm == kPifIndex.v)
        return FileRoute::Pif;
    if (arm == kDdDiskIndex.v)
        return FileRoute::DiskImage;
    if (arm == kDdIplIndex.v)
        return FileRoute::Ipl;
    if (arm == kGbIndex.v)
        return FileRoute::GameBoy;
    return FileRoute::Rom;
}

[[nodiscard]] constexpr FileRoute stream_route(proto::IoIndex index) noexcept {
    return (index.v & 0x3fu) == kDdIplPreloadIndex.v ? FileRoute::Ipl : file_route(index);
}

}
