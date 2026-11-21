// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/hd/page_description.h"
#include "ui/row_cx.h"
#include "ui/transition.h"
#include "ui/types.h"

namespace mister::ui {

[[nodiscard]] constexpr bool focusable(const app::hd::PageDescription::Row& row) noexcept {
    return row.kind != app::hd::PageDescription::RowKind::Separator;
}

class IRow {
public:
    virtual ~IRow() = default;

    [[nodiscard]] virtual bool shown() const { return true; }

    [[nodiscard]] virtual std::optional<RowKey> key() const { return std::nullopt; }

    [[nodiscard]] virtual app::hd::PageDescription::Row describe(const RowCx& cx) const = 0;
    virtual Transition select(const RowCx& cx) = 0;

    virtual Transition step(CycleDir dir, const RowCx& cx) {
        (void)dir;
        (void)cx;
        return Transition{};
    }

protected:
    IRow() = default;
    IRow(const IRow&) = default;
    IRow& operator=(const IRow&) = default;
};

}
