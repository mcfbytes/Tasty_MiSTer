// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/connect_slot_plan.h"

#include <utility>

#include "ui/confirm_launch.h"
#include "ui/confirm_page.h"
#include "ui/connect_port.h"
#include "ui/hd_plan_inputs.h"
#include "ui/page_context.h"
#include "ui/text_entry_launch.h"
#include "infra/tally.h"

namespace mister::ui {

bool HdSlotRow::shown() const {
    TASTY_SEAT_BODY(HdSlotRow);
    return owner_->slot_shown(slot_);
}

app::hd::PageDescription::Row HdSlotRow::describe(const RowCx&) const {
    TASTY_SEAT_BODY(HdSlotRow);
    return owner_->slot_describe(slot_);
}

Transition HdSlotRow::select(const RowCx&) {
    TASTY_SEAT_BODY(HdSlotRow);
    return owner_->slot_select(slot_);
}

ConnectSlotPlan::ConnectSlotPlan(const HdPlanInputs& in, RowCount slots) : ctx_(in.ctx) {
    TASTY_SEAT_BODY(ConnectSlotPlan);
    for (const RowIndex at : ui::rows(slots))
        slots_.emplace_back(static_cast<IHdSlots&>(*this), SlotId::at(at));
    for (HdSlotRow& row : slots_)
        refs_.emplace_back(row);
}

std::span<const std::reference_wrapper<IRow>> ConnectSlotPlan::rows() const {
    TASTY_SEAT_BODY(ConnectSlotPlan);
    return refs_;
}

infra::Tally ConnectSlotPlan::refusals() const noexcept {
    TASTY_SEAT_BODY(ConnectSlotPlan);
    return refusals_;
}

const ConnectViews& ConnectSlotPlan::views() const noexcept {
    return ctx_.connect ? ctx_.connect->views() : none_;
}

bool ConnectSlotPlan::send(const svc::connect::ConnectRequest& request) {
    if (ctx_.connect && ctx_.connect->send(request))
        return true;
    refusals_.add();
    return false;
}

Transition ConnectSlotPlan::ask(
    SlotId slot,
    std::string_view title,
    std::span<const std::string_view> body,
    std::span<const std::string_view> choices,
    RowIndex initial,
    bool replace
) {
    if (!ctx_.launches.confirm) {
        refusals_.add();
        return Transition{};
    }
    asked_ = slot;
    ConfirmPage::Spec spec{};
    spec.title = title;
    spec.body = body;
    spec.choices = choices;
    spec.host = infra::OptRef<IConfirmHost>{static_cast<IConfirmHost&>(*this)};
    spec.initial = initial;
    return ctx_.launches.confirm->open(PageId::Confirm, spec, replace);
}

Transition ConnectSlotPlan::request_text(
    SlotId slot,
    TextEntryKind kind,
    std::string_view title,
    bool replace
) {
    if (!ctx_.launches.entry) {
        refusals_.add();
        return Transition{};
    }
    asked_ = slot;
    TextEntryRequest req{
        .kind = kind,
        .title = title,
        .host = infra::OptRef<ITextEntryHost>{static_cast<ITextEntryHost&>(*this)}
    };
    return ctx_.launches.entry->open(PageId::TextEntry, std::move(req), replace);
}

Transition ConnectSlotPlan::on_confirm(RowIndex choice) {
    TASTY_SEAT_BODY(ConnectSlotPlan);
    return slot_confirmed(asked_, choice);
}

Transition ConnectSlotPlan::on_entered(const TextEntryResult& result) {
    TASTY_SEAT_BODY(ConnectSlotPlan);
    return slot_entered(asked_, result);
}

}
