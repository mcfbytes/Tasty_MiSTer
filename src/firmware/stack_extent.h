// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::fw {

inline constexpr std::size_t kStackPageBytes = 4096;

inline constexpr std::size_t kStackPrefaultReserveBytes = 64u * 1024u;

struct StackExtent {
    std::size_t used = 0;
    std::size_t mapped = 0;
    std::uint64_t rlimit = 0;

    [[nodiscard]] constexpr std::size_t bound(bool growable) const noexcept {
        if (growable && rlimit > mapped) {
            return rlimit > static_cast<std::uint64_t>(SIZE_MAX) ? SIZE_MAX
                                                                 : static_cast<std::size_t>(rlimit);
        }
        return mapped;
    }

    [[nodiscard]] constexpr std::size_t span(std::size_t requested, bool growable) const noexcept {
        std::uint64_t limit = bound(growable);
        if (rlimit < limit)
            limit = rlimit;
        const std::uint64_t floor = static_cast<std::uint64_t>(used) + kStackPrefaultReserveBytes;
        if (limit <= floor)
            return 0;
        const std::uint64_t room = limit - floor;
        return room < requested ? static_cast<std::size_t>(room) : requested;
    }
};

}
