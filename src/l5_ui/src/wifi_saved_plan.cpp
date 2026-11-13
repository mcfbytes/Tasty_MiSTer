// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/wifi_saved_plan.h"

#include <array>

#include <optional>

#include "infra/message_sum.h"
#include "infra/opt_ref.h"
#include "svc/connect_request.h"
#include "ui/connect_text.h"
#include "ui/hd_plan_inputs.h"
#include "ui/slot_range.h"

namespace mister::ui {
namespace {

using RowKind = app::PageDescription::RowKind;
using Request = svc::ConnectRequest;

constexpr std::array<std::string_view, 2> kYesNo{"Yes", "No"};
constexpr std::array<std::string_view, 1> kBody{"It will not reconnect by itself."};
constexpr RowIndex kOnNo = RowIndex::of(1);
constexpr RowIndex kYes = RowIndex::of(0);
constexpr std::string_view kHidden = "(hidden)";
constexpr SlotRange kSaved = SlotRange::table(RowCount::of(svc::WifiSavedView::kRows));

infra::OptRef<const svc::WifiSavedViewRow> saved_at(const ConnectViews& views, SlotId slot) {
    const std::optional<std::size_t> at = kSaved.subscript_of(slot);
    if (!views.saved || !at)
        return std::nullopt;
    const auto& row = views.saved->rows[*at];
    if (!row)
        return {};
    return infra::OptRef<const svc::WifiSavedViewRow>{*row};
}

}

WifiSavedPlan::WifiSavedPlan(const HdPlanInputs& in) : ConnectSlotPlan(in, kSaved.total()) {
    TASTY_SEAT_BODY(WifiSavedPlan);
}

PageId WifiSavedPlan::id() const {
    TASTY_SEAT_BODY(WifiSavedPlan);
    return PageId::WifiSaved;
}

std::string_view WifiSavedPlan::title() const {
    TASTY_SEAT_BODY(WifiSavedPlan);
    return "Saved networks";
}

bool WifiSavedPlan::remembers_focus() const noexcept {
    TASTY_SEAT_BODY(WifiSavedPlan);
    return false;
}

ConnectDemand WifiSavedPlan::connect_demand() const {
    TASTY_SEAT_BODY(WifiSavedPlan);
    return ConnectDemand::wifi_page();
}

bool WifiSavedPlan::slot_shown(SlotId slot) const {
    TASTY_SEAT_BODY(WifiSavedPlan);
    return saved_at(views(), slot).has_value();
}

app::PageDescription::Row WifiSavedPlan::slot_describe(SlotId slot) const {
    TASTY_SEAT_BODY(WifiSavedPlan);
    infra::OptRef<const svc::WifiSavedViewRow> row = saved_at(views(), slot);
    if (!row)
        return connect_row(RowKind::Action, kHidden);
    const os::Ssid::Display shown = row->ssid.display();
    const std::string_view mark = row->current ? "connected" : (row->disabled ? "disabled" : "");
    return connect_row(RowKind::Value, shown.view().empty() ? kHidden : shown.view(), mark);
}

Transition WifiSavedPlan::slot_select(SlotId slot) {
    TASTY_SEAT_BODY(WifiSavedPlan);
    infra::OptRef<const svc::WifiSavedViewRow> row = saved_at(views(), slot);
    if (!row)
        return Transition{};
    asked_ = row->ssid;
    scratch_.assign("Forget ").append(row->ssid.display().view()).append("?");
    return ask(slot, scratch_, kBody, kYesNo, kOnNo, false);
}

Transition WifiSavedPlan::slot_confirmed(SlotId slot, RowIndex choice) {
    TASTY_SEAT_BODY(WifiSavedPlan);
    (void)slot;
    if (choice == kYes)
        (void)send(infra::make<Request>(Request::WifiForget{asked_}));
    return Transition::pop();
}

}
