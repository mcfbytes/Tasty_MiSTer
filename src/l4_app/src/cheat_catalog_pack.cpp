// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/cheat_catalog_pack.h"

#include <cstddef>

namespace mister::app {

CheatCatalog pack_cheat_catalog(const cores::ICheatRecords& records) noexcept {
    CheatCatalog out{};
    out.geometry = records.cheat_geometry();
    const std::size_t have = records.cheat_count();
    for (std::size_t i = 0; i < have; ++i) {
        (void)cheat_catalog_add(out, records.cheat_name(i), records.cheat_bytes(i));
    }
    return out;
}

}
