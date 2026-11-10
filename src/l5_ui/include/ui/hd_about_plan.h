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

    void refresh(const SystemFacts& facts, const SessionFacts& session) override;

private:
    struct AboutFacts {
        std::string version;
        std::string os_version;
        std::string kernel;
        std::string storage;
        friend bool operator==(const AboutFacts&, const AboutFacts&) = default;
    };

    [[nodiscard]] static AboutFacts about_of(const SystemFacts& facts);
    void build_();

    AboutFacts shown_{};
    std::vector<HdValueRow> rows_;
    std::vector<std::reference_wrapper<IHdRow>> refs_;
};

}
