// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "ui/row.h"

namespace mister::ui {

class HdGatedRow final : public IRow {
    TASTY_SEAT_RESIDENT(Ui);

public:
    HdGatedRow(IRow& row, const bool& when) noexcept : row_(row), when_(when) {}

    bool shown() const override;
    std::optional<RowKey> key() const override;

    app::hd::PageDescription::Row describe(const RowCx& cx) const override;
    Transition select(const RowCx& cx) override;
    Transition step(CycleDir dir, const RowCx& cx) override;

private:
    IRow& row_;
    const bool& when_;
};

}
