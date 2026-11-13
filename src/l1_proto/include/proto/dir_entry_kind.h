// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace mister::proto {

enum class DirEntryKind : std::uint32_t { Pad = 1, Sector = 2, Osd = 3, Sys = 4, Lane = 5 };
static_assert(std::to_underlying(DirEntryKind::Pad) == 1);
static_assert(std::to_underlying(DirEntryKind::Sector) == 2);
static_assert(std::to_underlying(DirEntryKind::Osd) == 3);
static_assert(std::to_underlying(DirEntryKind::Sys) == 4);
static_assert(std::to_underlying(DirEntryKind::Lane) == 5);

[[nodiscard]] constexpr std::optional<DirEntryKind> kind_of_wire(std::uint32_t w) noexcept {
    constexpr DirEntryKind kKinds[]{
        DirEntryKind::Pad,
        DirEntryKind::Sector,
        DirEntryKind::Osd,
        DirEntryKind::Sys,
        DirEntryKind::Lane
    };
    for (const DirEntryKind kind : kKinds)
        if (w == std::to_underlying(kind))
            return kind;
    return std::nullopt;
}

}
