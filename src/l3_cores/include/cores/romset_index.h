// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "cores/romset_alias.h"
#include "infra/seat.h"

namespace mister::cores::neogeo {

inline constexpr std::size_t kRomsetIndexMax = 1000;

inline constexpr std::uint32_t kVromSize = 16u * 1024u * 1024u;

struct RomsetEntry {
    std::string name;

    std::string altname;
    bool hide = false;
};

struct RomsetIndex {
    TASTY_SEAT_EXEMPT(component);
    std::vector<RomsetEntry> entries;

    void scan(std::string_view xml);

    [[nodiscard]] RomsetAlias lookup(std::string_view key) const;
};

}
