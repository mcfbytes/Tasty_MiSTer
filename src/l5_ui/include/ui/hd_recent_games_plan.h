// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

#include "infra/seat.h"
#include "ui/hd_plan.h"
#include "ui/recent_row.h"

namespace mister::ui {

struct HdPlanInputs;

class HdRecentGamesPlan final : public IHdPlan {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit HdRecentGamesPlan(const HdPlanInputs& in);
    HdRecentGamesPlan(const HdRecentGamesPlan&) = delete;
    HdRecentGamesPlan& operator=(const HdRecentGamesPlan&) = delete;

    PageId id() const override;
    std::string_view title() const override;
    std::span<const std::reference_wrapper<IHdRow>> rows() const override;
    std::uint32_t refusals() const noexcept override;

private:
    RecentLaunch launch_;
    std::vector<RecentRow> rows_;
    std::vector<std::reference_wrapper<IHdRow>> refs_;
    std::uint32_t refusals_ = 0;
};

}
