// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "cores/n64_memory_type.h"
#include "cores/n64_save_layout_key.h"
#include "cores/save_row_count.h"
#include "cores/types.h"
#include "infra/indexed_array.h"

namespace mister::cores::n64 {

struct RowExtent {
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
};

struct SaveLayout {

    struct Hit {
        SaveRow row{};
        RowExtent rest{};
    };

    infra::IndexedArray<RowExtent, kMaxSaveRows, SaveRow> rows{};
    SaveRowCount count{};

    bool valid = false;

    [[nodiscard]] constexpr std::optional<Hit> locate(std::uint64_t absolute) const noexcept {
        for (SaveRow row{}; row != count.end(); row = row.next()) {
            const RowExtent& r = rows[row];
            if (absolute < static_cast<std::uint64_t>(r.offset) + r.length) {
                const auto within = static_cast<std::uint32_t>(absolute - r.offset);
                return Hit{row, RowExtent{within, r.length - within}};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<RowExtent> last() const noexcept {
        if (count.none())
            return std::nullopt;
        return rows[count.last()];
    }
};

[[nodiscard]] constexpr SaveLayout expand_save_layout(SaveLayoutKey key) noexcept {
    SaveLayout out{};
    const MemoryType cart = key.cart;
    const bool tpak = key.tpak;
    if (key.rows.none() || !key.rows.fits())
        return out;
    std::uint32_t prev = save_offset(SaveRowCount{}, cart, tpak);
    for (SaveRow row{}; row != key.rows.end(); row = row.next()) {
        const std::uint32_t next = save_offset(SaveRowCount::through(row), cart, tpak);
        if (next <= prev)
            return SaveLayout{};
        out.rows[row] = RowExtent{prev, next - prev};
        prev = next;
    }
    out.count = key.rows;
    out.valid = true;
    return out;
}

}
