// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/bt_devices_plan.h"

#include <array>

#include "infra/message_sum.h"
#include "infra/opt_ref.h"
#include "svc/bt_device_view.h"
#include "svc/bt_page_mode.h"
#include "svc/connect_request.h"
#include "ui/connect_text.h"
#include "ui/hd_plan_inputs.h"
#include "ui/slot_range.h"

namespace mister::ui {
namespace {

using RowKind = app::PageDescription::RowKind;
using Request = svc::ConnectRequest;

constexpr std::array<std::string_view, 3> kWhenIdle{"Connect", "Remove", "Back"};
constexpr std::array<std::string_view, 3> kWhenConnected{"Disconnect", "Remove", "Back"};
constexpr std::array<std::string_view, 2> kYesNo{"Yes", "No"};
constexpr SlotRange kPaired = SlotRange::table(RowCount::of(svc::BtDeviceView::kRows));
constexpr RowIndex kAct = RowIndex::of(0);
constexpr RowIndex kRemove = RowIndex::of(1);
constexpr RowIndex kYes = RowIndex::of(0);
constexpr RowIndex kOnNo = RowIndex::of(1);

infra::OptRef<const svc::BtDeviceViewRow> paired_at(const ConnectViews& views, SlotId slot) {
    const std::optional<std::size_t> want = kPaired.subscript_of(slot);
    if (!views.devices || !want)
        return std::nullopt;
    std::size_t seen = 0;
    for (const auto& row : views.devices->rows) {
        if (!row)
            break;
        if (!row->paired)
            continue;
        if (seen++ == *want)
            return infra::OptRef<const svc::BtDeviceViewRow>{*row};
    }
    return std::nullopt;
}

}

BtDevicesPlan::BtDevicesPlan(const HdPlanInputs& in) : ConnectSlotPlan(in, kPaired.total()) {
    TASTY_SEAT_BODY(BtDevicesPlan);
}

PageId BtDevicesPlan::id() const {
    TASTY_SEAT_BODY(BtDevicesPlan);
    return PageId::BtDevices;
}

std::string_view BtDevicesPlan::title() const {
    TASTY_SEAT_BODY(BtDevicesPlan);
    return "Paired controllers";
}

bool BtDevicesPlan::remembers_focus() const noexcept {
    TASTY_SEAT_BODY(BtDevicesPlan);
    return false;
}

ConnectDemand BtDevicesPlan::connect_demand() const {
    TASTY_SEAT_BODY(BtDevicesPlan);
    return ConnectDemand::bluetooth(svc::BtPageMode::Paired, PairStart::Pick);
}

bool BtDevicesPlan::slot_shown(SlotId slot) const {
    TASTY_SEAT_BODY(BtDevicesPlan);
    return paired_at(views(), slot).has_value();
}

app::PageDescription::Row BtDevicesPlan::slot_describe(SlotId slot) const {
    TASTY_SEAT_BODY(BtDevicesPlan);
    const infra::OptRef<const svc::BtDeviceViewRow> row = paired_at(views(), slot);
    if (!row)
        return connect_row(RowKind::Action, {});
    const std::string name{row->name.empty() ? row->addr.format().view() : row->name.view()};
    return connect_row(RowKind::Value, name, row->connected ? "connected" : "");
}

Transition BtDevicesPlan::slot_select(SlotId slot) {
    TASTY_SEAT_BODY(BtDevicesPlan);
    const infra::OptRef<const svc::BtDeviceViewRow> row = paired_at(views(), slot);
    if (!row)
        return Transition{};
    asked_ = row->addr;
    asked_connected_ = row->connected;
    removing_ = false;
    scratch_.assign(row->name.empty() ? row->addr.format().view() : row->name.view());
    return ask(slot, scratch_, {}, asked_connected_ ? kWhenConnected : kWhenIdle, kAct, false);
}

Transition BtDevicesPlan::slot_confirmed(SlotId slot, RowIndex choice) {
    TASTY_SEAT_BODY(BtDevicesPlan);
    if (removing_) {
        if (choice == kYes)
            (void)send(infra::make<Request>(Request::BtRemove{asked_}));
        return Transition::pop();
    }
    if (choice == kAct) {
        (void)send(
            asked_connected_ ? infra::make<Request>(Request::BtDisconnect{asked_})
                             : infra::make<Request>(Request::BtConnect{asked_})
        );
        return Transition::pop();
    }
    if (choice == kRemove) {
        removing_ = true;
        std::string title{"Remove "};
        title.append(scratch_).append("?");
        scratch_ = title;
        return ask(slot, scratch_, {}, kYesNo, kOnNo, true);
    }
    return Transition::pop();
}

}
