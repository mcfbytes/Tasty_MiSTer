// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::proto {

struct DiskProtocol {
    std::uint8_t v = 0;
    static const DiskProtocol None;
    static const DiskProtocol Acsi;
    friend constexpr bool operator==(DiskProtocol, DiskProtocol) = default;
};

inline constexpr DiskProtocol DiskProtocol::None{0};
inline constexpr DiskProtocol DiskProtocol::Acsi{1};

}
