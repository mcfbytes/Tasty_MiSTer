// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>
#include <optional>

#include "app/page_description.h"
#include "ui/transition.h"
#include "ui/types.h"

namespace mister::ui {

static_assert(
    RowKey::in_space(RowKeySpace::Push, ItemId{std::numeric_limits<std::uint16_t>::max()})
        != RowKey::in_space(RowKeySpace::Slot, ItemId{})
    && RowKey::in_space(RowKeySpace::Slot, ItemId{std::numeric_limits<std::uint16_t>::max()})
        != RowKey::in_space(RowKeySpace::Push, ItemId{})
);

class IHdRow {
public:
    virtual ~IHdRow() = default;

    [[nodiscard]] virtual bool shown() const { return true; }

    [[nodiscard]] virtual std::optional<RowKey> key() const { return std::nullopt; }

    [[nodiscard]] virtual app::PageDescription::Row describe() const = 0;

    [[nodiscard]] virtual bool focusable() const {
        return describe().kind != app::PageDescription::RowKind::Separator;
    }

    [[nodiscard]] virtual app::RowTier tier() const { return describe().tier; }

    [[nodiscard]] virtual bool has_steps() const { return describe().steps; }

    virtual Transition select() = 0;

    virtual Transition step(int delta) {
        (void)delta;
        return Transition{};
    }

protected:
    IHdRow() = default;
    IHdRow(const IHdRow&) = default;
    IHdRow& operator=(const IHdRow&) = default;
};

}
