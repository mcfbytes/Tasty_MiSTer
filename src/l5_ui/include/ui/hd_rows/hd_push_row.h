// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "app/hd/page_description.h"
#include "app/hd/tier_set.h"
#include "infra/seat.h"
#include "ui/row.h"
#include "ui/page_arg.h"
#include "ui/page_id.h"
#include "ui/types.h"

namespace mister::ui {

class HdPushRow final : public IRow {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Spec {
        std::string_view label;
        PageId page = PageId::None;
        PageArg arg{};
        app::hd::RowTier tier = app::hd::RowTier::Place;
        app::hd::PageDescription::RowIcon icon = app::hd::PageDescription::RowIcon::None;
    };

    explicit HdPushRow(const Spec& spec) noexcept : spec_(spec) {}

    std::optional<RowKey> key() const override;
    app::hd::PageDescription::Row describe(const RowCx&) const override;
    Transition select(const RowCx&) override;

private:
    Spec spec_;
};

}
