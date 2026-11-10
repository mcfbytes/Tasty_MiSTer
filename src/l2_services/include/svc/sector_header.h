// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>

namespace mister::svc {

inline constexpr std::size_t kSectorHeaderBytes = 16;

struct SectorHeader {
    std::array<std::byte, kSectorHeaderBytes> bytes{};
    bool named = false;
    friend constexpr bool operator==(SectorHeader, SectorHeader) = default;
};

}
