// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "ui/hd_row.h"
#include "ui/options_page.h"

namespace mister::ui {

class IOptionsSurface;

class HdOptionRow final : public IHdRow {
    TASTY_SEAT_RESIDENT(Ui);

public:
    HdOptionRow(const OptionItem& item, IOptionsSurface& host) noexcept
        : item_(item),
          host_(host) {}

    std::optional<RowKey> key() const override;
    app::PageDescription::Row describe() const override;
    Transition select() override;
    Transition step(int delta) override;

private:
    [[nodiscard]] OptionItem::Look look_() const;
    [[nodiscard]] OptionItem::Press press_(bool select, bool minus) const;

    const OptionItem& item_;
    IOptionsSurface& host_;
};

}
