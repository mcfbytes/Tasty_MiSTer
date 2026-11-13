// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "infra/seat.h"
#include "svc/connect_request.h"
#include "ui/confirm_host.h"
#include "ui/connect_views.h"
#include "ui/hd_plan.h"
#include "ui/hd_rows/hd_slot_row.h"
#include "ui/hd_slots.h"
#include "ui/text_entry_host.h"
#include "ui/text_entry_kind.h"
#include "svc/types.h"

namespace mister::ui {

struct HdPlanInputs;
struct PageContext;

class ConnectSlotPlan : public IHdPlan,
                        public IHdSlots,
                        private IConfirmHost,
                        private ITextEntryHost {
    TASTY_SEAT_RESIDENT(Ui);

public:
    ConnectSlotPlan(const ConnectSlotPlan&) = delete;
    ConnectSlotPlan& operator=(const ConnectSlotPlan&) = delete;

    std::span<const std::reference_wrapper<IHdRow>> rows() const override;
    svc::Tally refusals() const noexcept override;

protected:
    ConnectSlotPlan(const HdPlanInputs& in, RowCount slots);

    [[nodiscard]] const ConnectViews& views() const noexcept;

    bool send(const svc::ConnectRequest& request);

    Transition ask(
        SlotId slot,
        std::string_view title,
        std::span<const std::string_view> body,
        std::span<const std::string_view> choices,
        RowIndex initial,
        bool replace
    );

    Transition request_text(SlotId slot, TextEntryKind kind, std::string_view title, bool replace);

    PageContext& ctx_;
    std::string scratch_;

private:
    Transition on_confirm(RowIndex choice) override;
    Transition on_entered(const TextEntryResult& result) override;

    std::deque<HdSlotRow> slots_;
    std::vector<std::reference_wrapper<IHdRow>> refs_;
    ConnectViews none_{};
    SlotId asked_ = SlotId::at(RowIndex{});
    svc::Tally refusals_{};
};

}
