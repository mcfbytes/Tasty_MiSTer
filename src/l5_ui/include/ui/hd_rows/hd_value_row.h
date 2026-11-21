// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "app/hd/page_description.h"
#include "infra/seat.h"
#include "ui/row.h"

namespace mister::ui {

class HdValueRow final : public IRow {
    TASTY_SEAT_RESIDENT(Ui);

public:
    using Value = decltype(app::hd::PageDescription::Row::value);

    struct Spec {
        std::string_view label;
        Value value;
    };

    explicit HdValueRow(const Spec& spec) noexcept : spec_(spec) {}

    app::hd::PageDescription::Row describe(const RowCx&) const override;

    Transition select(const RowCx&) override;

private:
    Spec spec_;
};

}
