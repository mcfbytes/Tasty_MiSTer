// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/cheat_catalog_pack.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace mister::app {

namespace {

template <std::size_t N>
void copy_cell_text(char (&dst)[N], std::string_view src) noexcept {
    const std::size_t n = src.size() < N - 1 ? src.size() : N - 1;
    if (n != 0) std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}
}  // namespace

void pack_cheat_catalog(const cores::ICheatRecords& records, CheatCatalog& out) noexcept {
    out = CheatCatalog{};
    const cores::CheatGeometry geom = records.cheat_geometry();
    out.unit = geom.unit;
    out.max_active = geom.max_active;
    const std::size_t have = records.cheat_count();
    for (std::size_t i = 0; i < have; ++i) {
        const std::span<const std::uint8_t> bytes = records.cheat_bytes(i);
        const std::size_t used = out.used;
        if (out.count >= CheatCatalog::kMaxRows ||
            bytes.size() > CheatCatalog::kArenaBytes - used) {
            ++out.dropped;
            continue;
        }
        CheatCatalog::Row& row = out.rows[out.count];
        copy_cell_text(row.name, records.cheat_name(i));
        row.offset = static_cast<std::uint16_t>(used);
        row.len = static_cast<std::uint16_t>(bytes.size());
        if (!bytes.empty()) std::memcpy(out.data + used, bytes.data(), bytes.size());
        out.used = static_cast<std::uint16_t>(used + bytes.size());
        ++out.count;
    }
}

}  // namespace mister::app
