// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "svc/disc_engine.h"

namespace mister::svc {

inline constexpr std::size_t kTocFrameBytes = 1600;
inline constexpr std::size_t kTocFrameTracks = 99;

enum class DiscRegion : std::uint8_t { Unknown = 0, Japan = 1, Usa = 2, Europe = 3 };

struct TocFrameMeta {
    DiscRegion region = DiscRegion::Unknown;
    bool reset_request = false;
    std::uint16_t libcrypt_mask = 0;
};

std::array<std::byte, kTocFrameBytes> build_toc_frame(const Toc& toc,
                                                      const TocFrameMeta& meta) noexcept;

}  // namespace mister::svc
