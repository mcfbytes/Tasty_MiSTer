// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "infra/seat.h"
#include "ui/hd_row.h"

namespace mister::ui {

class HdCloseRow final : public IHdRow {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Spec {
        std::string_view label;
    };

    explicit HdCloseRow(const Spec& spec) noexcept : spec_(spec) {}

    app::PageDescription::Row describe() const override;
    Transition select() override;

private:
    Spec spec_;
};

}
