// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "svc/disc/toc.h"
#include "svc/types.h"

namespace mister::cores::mac {

inline constexpr std::size_t kMcdaTocBytes = 1024;

inline constexpr std::size_t kMcdaTocTracks = 99;

[[nodiscard]] std::array<std::byte, kMcdaTocBytes> mcda_toc(
    const svc::disc::Toc& toc,
    std::optional<svc::TrackIndex> data_track
) noexcept;

[[nodiscard]] std::optional<svc::TrackIndex> first_data_track(const svc::disc::Toc& toc) noexcept;

}
