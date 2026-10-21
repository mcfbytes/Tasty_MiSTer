// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "ui/hd_row.h"

namespace mister::ui {

class IOptionsSurface;
class SavedBaseline;

class HdSaveRow final : public IHdRow {
    TASTY_SEAT_RESIDENT(Ui);

public:
    HdSaveRow(IOptionsSurface& host, const SavedBaseline& baseline) noexcept
        : host_(host),
          baseline_(baseline) {}

    std::optional<RowKey> key() const override;
    app::PageDescription::Row describe() const override;
    Transition select() override;

private:
    IOptionsSurface& host_;
    const SavedBaseline& baseline_;
};

}
