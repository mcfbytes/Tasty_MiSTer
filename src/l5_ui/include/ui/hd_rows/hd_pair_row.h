// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/page_description.h"
#include "infra/seat.h"
#include "ui/hd_row.h"

namespace mister::ui {

struct PageContext;

class HdPairRow final : public IHdRow {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit HdPairRow(PageContext& ctx, app::RowTier tier = app::RowTier::Place) noexcept
        : ctx_(ctx),
          tier_(tier) {}

    bool shown() const override;
    app::PageDescription::Row describe() const override;
    Transition select() override;

private:
    PageContext& ctx_;
    app::RowTier tier_;
};

}
