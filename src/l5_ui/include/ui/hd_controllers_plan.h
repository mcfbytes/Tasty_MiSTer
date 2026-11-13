// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "infra/seat.h"
#include "ui/hd_plan.h"
#include "ui/hd_rows/hd_push_row.h"
#include "svc/types.h"

namespace mister::ui {

struct HdPlanInputs;

class HdControllersPlan final : public IHdPlan {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit HdControllersPlan(const HdPlanInputs& in);
    HdControllersPlan(const HdControllersPlan&) = delete;
    HdControllersPlan& operator=(const HdControllersPlan&) = delete;

    PageId id() const override;
    std::string_view title() const override;
    std::span<const std::reference_wrapper<IHdRow>> rows() const override;
    svc::Tally refusals() const noexcept override;

private:
    svc::Tally refusals_{};
    HdPushRow pair_;
    HdPushRow bluetooth_;
    std::string define_label_;
    HdPushRow define_;
    std::vector<std::reference_wrapper<IHdRow>> refs_;
};

}
