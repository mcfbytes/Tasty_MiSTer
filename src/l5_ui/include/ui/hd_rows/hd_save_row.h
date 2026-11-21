// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "ui/row.h"

namespace mister::ui {

class IOptionsSurface;
class SavedBaseline;

class HdSaveRow final : public IRow {
    TASTY_SEAT_RESIDENT(Ui);

public:
    HdSaveRow(IOptionsSurface& host, const SavedBaseline& baseline) noexcept
        : host_(host),
          baseline_(baseline) {}

    std::optional<RowKey> key() const override;
    app::hd::PageDescription::Row describe(const RowCx&) const override;
    Transition select(const RowCx&) override;

private:
    IOptionsSurface& host_;
    const SavedBaseline& baseline_;
};

}
