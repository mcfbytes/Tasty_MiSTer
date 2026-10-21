// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "infra/seat.h"
#include "ui/hd_row.h"
#include "ui/page_id.h"
#include "ui/types.h"

namespace mister::ui {

class HdPushRow final : public IHdRow {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Spec {
        std::string_view label;
        PageId page = PageId::None;
        ItemId focus = kNoItem;
    };

    explicit HdPushRow(const Spec& spec) noexcept : spec_(spec) {}

    std::optional<RowKey> key() const override;
    app::PageDescription::Row describe() const override;
    Transition select() override;

private:
    Spec spec_;
};

}
