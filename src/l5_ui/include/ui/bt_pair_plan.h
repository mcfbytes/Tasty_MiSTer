// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>

#include "infra/seat.h"
#include "svc/prompt_gen.h"
#include "ui/bt_prompt_rows.h"
#include "ui/connect_slot_plan.h"
#include "ui/pair_start.h"

namespace mister::ui {

class BtPairPlan final : public ConnectSlotPlan {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit BtPairPlan(const HdPlanInputs& in);

    PageId id() const override;
    std::string_view title() const override;
    bool remembers_focus() const noexcept override;
    ConnectDemand connect_demand() const override;
    void on_focus_moved() override;
    bool holds_pairing() const override;

    bool slot_shown(SlotId slot) const override;
    app::PageDescription::Row slot_describe(SlotId slot) const override;
    Transition slot_select(SlotId slot) override;
    Transition slot_entered(SlotId slot, const TextEntryResult& result) override;

private:
    [[nodiscard]] std::optional<BtPromptRows> prompt_() const;
    bool answer_(svc::PromptAnswer answer);

    PairStart start_;
    bool show_all_ = false;
    std::optional<svc::PromptGen> entry_gen_{};
};

}
