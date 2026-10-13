// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>
#include <utility>

#include "infra/error.h"
#include "proto/bit_field.h"
#include "proto/item_probe.h"
#include "proto/item_table.h"
#include "proto/standard_item_row.h"

namespace mister::proto {

namespace detail {

[[nodiscard]] constexpr std::optional<ItemProbe> standard_toggle_probe_of(
    BitField bits, unsigned value_count, std::string_view v0, std::string_view v1) noexcept {
    if (bits.width != 1 || value_count != 2) return std::nullopt;
    const auto is_on = [](std::string_view v) {
        return standard_label_matches(v, "on") || standard_label_matches(v, "yes");
    };
    const auto is_off = [](std::string_view v) {
        return standard_label_matches(v, "off") || standard_label_matches(v, "no");
    };
    if (is_off(v0) && is_on(v1)) return ItemProbe{bits.start, 1, 1};
    if (is_on(v0) && is_off(v1)) return ItemProbe{bits.start, 1, 0};
    return std::nullopt;
}

[[nodiscard]] constexpr std::optional<ItemProbe> standard_probe_of(ItemShape shape, BitField bits,
                                                                   unsigned value_count,
                                                                   std::string_view v0,
                                                                   std::string_view v1) noexcept {
    switch (shape) {
        case ItemShape::Toggle:
            return standard_toggle_probe_of(bits, value_count, v0, v1);
    }
    return std::nullopt;
}

}  // namespace detail

[[nodiscard]] inline std::optional<ItemProbe> find_standard_item(const ItemTable& table,
                                                                 StandardItem item) noexcept {
    if (item == StandardItem::kCount)
        fatal(Error{Errc::not_found, ERR_SITE(), 0}, "find_standard_item: kCount is not an item");
    const StandardItemRow& row = kStandardItemRows[std::to_underlying(item)];
    std::optional<ItemProbe> found;
    for (const auto& it : table.items()) {
        if (it.kind != ItemKind::Option || !it.bits.valid()) continue;
        if (!detail::standard_row_matches(row, table.subfield(it, 1))) continue;
        const unsigned value_count = it.subfield_count > 2 ? it.subfield_count - 2u : 0u;
        const std::optional<ItemProbe> probe = detail::standard_probe_of(
            row.shape, it.bits, value_count, table.subfield(it, 2), table.subfield(it, 3));
        if (!probe) continue;
        if (found) return std::nullopt;
        found = probe;
    }
    return found;
}

}  // namespace mister::proto
