// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

#include "infra/seat.h"
#include "ui/hd_plan.h"
#include "ui/hd_rows/hd_action.h"
#include "ui/hd_rows/hd_ask_row.h"

namespace mister::ui {

class OsdStylePreference;
struct HdPlanInputs;
struct PageContext;

class HdDisplayPlan final : public IHdPlan {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit HdDisplayPlan(const HdPlanInputs& in);
    HdDisplayPlan(const HdDisplayPlan&) = delete;
    HdDisplayPlan& operator=(const HdDisplayPlan&) = delete;

    PageId id() const override;
    std::string_view title() const override;
    std::span<const std::reference_wrapper<IHdRow>> rows() const override;
    std::uint32_t refusals() const noexcept override;
    [[nodiscard]] std::uint32_t commands_published() const noexcept;

private:
    class ClassicAction final : public IHdAction {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        explicit ClassicAction(HdDisplayPlan& plan) noexcept : plan_(plan) {}

        Transition on_yes() override;

    private:
        HdDisplayPlan& plan_;
    };

    PageContext& ctx_;
    OsdStylePreference& style_;
    std::uint32_t refusals_ = 0;
    std::uint32_t published_ = 0;
    ClassicAction classic_{*this};
    HdAskRow style_row_;
    std::vector<std::reference_wrapper<IHdRow>> refs_;
};

}
