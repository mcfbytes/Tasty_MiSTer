// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "infra/seat.h"
#include "ui/hd_plan.h"
#include "ui/hd_rows/hd_value_row.h"

namespace mister::ui {

struct HdPlanInputs;

class HdCoreAboutPlan final : public IHdPlan {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit HdCoreAboutPlan(const HdPlanInputs& in);
    HdCoreAboutPlan(const HdCoreAboutPlan&) = delete;
    HdCoreAboutPlan& operator=(const HdCoreAboutPlan&) = delete;

    PageId id() const override;
    std::string_view title() const override;
    std::span<const std::reference_wrapper<IHdRow>> rows() const override;

private:
    struct Line {
        std::string label;
        HdValueRow::Value value;
    };

    std::vector<Line> lines_;
    std::vector<HdValueRow> rows_;
    std::vector<std::reference_wrapper<IHdRow>> refs_;
};

}
