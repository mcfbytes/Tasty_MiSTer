// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

#include "infra/seat.h"
#include "ui/hd_plan.h"
#include "ui/hd_rows/hd_value_row.h"
#include "ui/system_facts.h"

namespace mister::ui {

struct HdPlanInputs;

class HdAboutPlan final : public IHdPlan {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit HdAboutPlan(const HdPlanInputs& in);
    HdAboutPlan(const HdAboutPlan&) = delete;
    HdAboutPlan& operator=(const HdAboutPlan&) = delete;

    PageId id() const override;
    std::string_view title() const override;
    std::span<const std::reference_wrapper<IHdRow>> rows() const override;

    void refresh(const SystemFacts& facts) override;

private:
    void build_();
    [[nodiscard]] std::string_view kernel_word_() const noexcept;

    SystemFacts facts_{};
    std::vector<HdValueRow> rows_;
    std::vector<std::reference_wrapper<IHdRow>> refs_;
};

}
