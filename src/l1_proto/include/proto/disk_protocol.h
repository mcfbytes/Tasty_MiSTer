// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::proto {

struct DiskProtocol {
    std::uint8_t v = 0;
    static const DiskProtocol None;
    static const DiskProtocol Acsi;

    static const DiskProtocol IsaBus;
    static const DiskProtocol AmigaFloppy;

    static constexpr std::size_t kCount = 4;
    friend constexpr bool operator==(DiskProtocol, DiskProtocol) = default;
};

inline constexpr DiskProtocol DiskProtocol::None{0};
inline constexpr DiskProtocol DiskProtocol::Acsi{1};
inline constexpr DiskProtocol DiskProtocol::IsaBus{2};
inline constexpr DiskProtocol DiskProtocol::AmigaFloppy{3};

}
