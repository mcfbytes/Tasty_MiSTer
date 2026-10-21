// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "ui/hd_row.h"

namespace mister::ui {

class HdGatedRow final : public IHdRow {
    TASTY_SEAT_RESIDENT(Ui);

public:
    HdGatedRow(IHdRow& row, const bool& when) noexcept : row_(row), when_(when) {}

    bool shown() const override;
    std::optional<RowKey> key() const override;
    app::PageDescription::Row describe() const override;
    Transition select() override;
    Transition step(int delta) override;

private:
    IHdRow& row_;
    const bool& when_;
};

}
