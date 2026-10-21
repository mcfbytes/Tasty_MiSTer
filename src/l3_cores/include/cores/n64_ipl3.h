// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/n64_db_tags.h"
#include "cores/n64_ipl3_row.h"

namespace mister::cores::n64 {

inline constexpr std::size_t kBootcodeBegin = 0x40;
inline constexpr std::size_t kBootcodeAleckEnd = 0xc00;
inline constexpr std::size_t kBootcodeEnd = 0x1000;

struct Ipl3Detect {
    SystemType system = SystemType::Unknown;
    Cic cic = Cic::Unknown;
    bool known_signature = true;
};

[[nodiscard]] constexpr std::array<std::uint64_t, 2> bootcode_checksums(
    std::span<const std::uint8_t> chunk
) noexcept {
    std::array<std::uint64_t, 2> sums{};
    if (chunk.size() < kBootcodeEnd)
        return sums;

    std::uint64_t sum = 0;
    std::size_t i = kBootcodeBegin;
    for (; i < kBootcodeAleckEnd; i += 4) {
        sum += static_cast<std::uint32_t>(chunk[i])
            | (static_cast<std::uint32_t>(chunk[i + 1]) << 8)
            | (static_cast<std::uint32_t>(chunk[i + 2]) << 16)
            | (static_cast<std::uint32_t>(chunk[i + 3]) << 24);
    }
    sums[1] = sum;
    for (; i < kBootcodeEnd; i += 4) {
        sum += static_cast<std::uint32_t>(chunk[i])
            | (static_cast<std::uint32_t>(chunk[i + 1]) << 8)
            | (static_cast<std::uint32_t>(chunk[i + 2]) << 16)
            | (static_cast<std::uint32_t>(chunk[i + 3]) << 24);
    }
    sums[0] = sum;
    return sums;
}

[[nodiscard]] constexpr SystemType region_system(char region_code) noexcept {
    switch (region_code) {
        case 'D':
        case 'F':
        case 'H':
        case 'I':
        case 'L':
        case 'P':
        case 'S':
        case 'U':
        case 'W':
        case 'X':
        case 'Y':
        case 'Z':
            return SystemType::Pal;
        default:
            return SystemType::Ntsc;
    }
}

[[nodiscard]] constexpr Ipl3Detect detect_from_bootcode(
    char region_code,
    std::span<const std::uint64_t> sums
) noexcept {
    Ipl3Detect out;
    out.system = region_system(region_code);
    if (sums.empty())
        return out;

    const Ipl3Row* row = nullptr;
    for (std::uint64_t sum : sums) {
        row = find_ipl3(sum);
        if (row != nullptr)
            break;
    }
    if (row == nullptr) {
        row = &kIpl3Default;
        out.known_signature = false;
    }
    if (row->force != SystemType::Unknown)
        out.system = row->force;
    out.cic = out.system != SystemType::Pal ? row->ntsc : row->pal;
    return out;
}

}
