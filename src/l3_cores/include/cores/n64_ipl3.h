// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/bootcode_sum.h"
#include "cores/n64_ipl3_row.h"
#include "cores/n64_system_type.h"

namespace mister::cores::n64 {

struct Ipl3Detect {
    SystemType system = SystemType::Unknown;
    Cic cic = Cic::Unknown;
    bool known_signature = true;
};

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
