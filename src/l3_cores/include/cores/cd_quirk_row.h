// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace mister::cores {

enum class CdQuirk : std::uint8_t {
    TocStartsAtIndex1,
    ReadSkipsSeek,
};

struct CdQuirkRow {
    std::uint32_t offset;
    std::string_view id;
    CdQuirk quirk;
};

[[nodiscard]] constexpr bool quirk_matches(std::span<const CdQuirkRow> rows, CdQuirk q,
                                           std::span<const std::uint8_t> header) noexcept {
    for (const CdQuirkRow& r : rows) {
        if (r.quirk != q) continue;
        if (r.offset > header.size() || header.size() - r.offset < r.id.size()) continue;
        bool same = true;
        for (std::size_t i = 0; i < r.id.size() && same; ++i)
            same = header[r.offset + i] == static_cast<std::uint8_t>(r.id[i]);
        if (same) return true;
    }
    return false;
}

}  // namespace mister::cores
