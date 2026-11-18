// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <utility>

#include "hal/types.h"
#include "os/types.h"
#include "proto/dir_entry_kind.h"

namespace mister::proto {

struct BlockLine {
    os::UioLine line{};
    hal::LwOffset cause{};
    friend constexpr bool operator==(BlockLine, BlockLine) = default;
};

enum class LineFrom : std::uint8_t { None, EntryWords, IrqRow };

inline constexpr std::array<std::pair<DirEntryKind, LineFrom>, 5> kLineSource{{
    {DirEntryKind::Pad, LineFrom::None},
    {DirEntryKind::Sector, LineFrom::IrqRow},
    {DirEntryKind::Osd, LineFrom::None},
    {DirEntryKind::Sys, LineFrom::EntryWords},
    {DirEntryKind::Lane, LineFrom::None},
}};

[[nodiscard]] constexpr LineFrom line_source(DirEntryKind kind) noexcept {
    for (const auto& [at, from] : kLineSource)
        if (at == kind)
            return from;
    return LineFrom::None;
}

static_assert(kLineSource.size() == 5, "one row per directory kind");

}
