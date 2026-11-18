// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "proto/bit_field.h"
#include "proto/item.h"
#include "proto/item_table.h"
#include "proto/reset_edge.h"
#include "proto/standard_item_row.h"
#include "proto/status_bit.h"
#include "proto/types.h"

namespace mister::proto {

struct SaveTrigger {
    ItemOrdinal item{};
    StatusBit bit{};
    bool ex = false;

    [[nodiscard]] constexpr ResetEdge edge() const noexcept {
        return ResetEdge::osd_toggle(bit, ex);
    }

    [[nodiscard]] bool usable(const ItemTable& table, OsdMask mask) const noexcept {
        const auto row = table.table().find(item);
        if (!row)
            return false;
        const Visibility vis = evaluate(*row, mask);
        return !vis.hidden && !vis.disabled;
    }

    friend constexpr bool operator==(SaveTrigger, SaveTrigger) = default;
};

[[nodiscard]] inline std::optional<SaveTrigger> find_save_trigger(const ItemTable& table) noexcept {
    const StandardItemRow& row = standard_row_of(StandardItem::SaveRow);
    const auto items = table.items();
    std::optional<SaveTrigger> found;
    for (std::uint16_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        if ((it.kind != ItemKind::Toggle && it.kind != ItemKind::ToggleClose) || !it.bits.valid())
            continue;
        if (!detail::standard_row_matches(row, table.subfield(it, 1)))
            continue;
        if (found)
            return std::nullopt;
        found = SaveTrigger{ItemOrdinal{i}, it.bits.start, it.ex == ExShift::Plus32};
    }
    if (found && found->bit == kResetBit)
        return std::nullopt;
    return found;
}

}
