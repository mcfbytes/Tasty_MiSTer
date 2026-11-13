// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/bluetooth_plan.h"

#include <array>

#include "infra/message_sum.h"
#include "svc/bt_page_mode.h"
#include "svc/connect_request.h"
#include "ui/connect_text.h"
#include "ui/hd_plan_inputs.h"

namespace mister::ui {
namespace {

using RowKind = app::PageDescription::RowKind;
using Request = svc::ConnectRequest;

enum class Slot : std::uint8_t { Status, Pair, Paired, Reset, RemoveAll, Count };

constexpr std::array<std::string_view, 2> kYesNo{"Yes", "No"};
constexpr std::array<std::string_view, 1> kRemoveBody{"Every paired controller is forgotten."};
constexpr RowIndex kOnNo = RowIndex::of(1);
constexpr RowIndex kYes = RowIndex::of(0);

bool has_paired(const ConnectViews& views) {
    if (!views.devices)
        return false;
    for (const auto& row : views.devices->rows) {
        if (!row)
            break;
        if (row->paired)
            return true;
    }
    return false;
}

}

BluetoothPlan::BluetoothPlan(const HdPlanInputs& in) : ConnectSlotPlan(in, named_rows<Slot>()) {
    TASTY_SEAT_BODY(BluetoothPlan);
}

PageId BluetoothPlan::id() const {
    TASTY_SEAT_BODY(BluetoothPlan);
    return PageId::Bluetooth;
}

std::string_view BluetoothPlan::title() const {
    TASTY_SEAT_BODY(BluetoothPlan);
    return "Bluetooth";
}

RowCount BluetoothPlan::pinned() const noexcept {
    TASTY_SEAT_BODY(BluetoothPlan);
    return named_rows<Slot>() - SlotId::of(Slot::Reset).row().before();
}

ConnectDemand BluetoothPlan::connect_demand() const {
    TASTY_SEAT_BODY(BluetoothPlan);
    return ConnectDemand::bluetooth(svc::BtPageMode::Paired, PairStart::Pick);
}

bool BluetoothPlan::slot_shown(SlotId slot) const {
    TASTY_SEAT_BODY(BluetoothPlan);
    if (slot.is(Slot::Paired))
        return has_paired(views());
    return true;
}

app::PageDescription::Row BluetoothPlan::slot_describe(SlotId slot) const {
    TASTY_SEAT_BODY(BluetoothPlan);
    switch (slot.named<Slot>()) {
        case Slot::Status:
            return connect_row(RowKind::Value, "Bluetooth", bt_cell(views().bt).view());
        case Slot::Pair:
            return connect_row(RowKind::Submenu, "Pair a controller");
        case Slot::Paired:
            return connect_row(RowKind::Submenu, "Paired controllers");
        case Slot::Reset:
            return connect_row(RowKind::Action, "Reset adapter");
        case Slot::RemoveAll:
        case Slot::Count:
            break;
    }
    return connect_row(RowKind::Action, "Remove all pairings");
}

Transition BluetoothPlan::slot_select(SlotId slot) {
    TASTY_SEAT_BODY(BluetoothPlan);
    switch (slot.named<Slot>()) {
        case Slot::Pair:
            return Transition::push(PageId::BtPair, PageArg{PairStart::Pick});
        case Slot::Paired:
            return Transition::push(PageId::BtDevices);
        case Slot::Reset:
            (void)send(infra::make<Request>(Request::BtResetAdapter{}));
            return Transition{};
        case Slot::RemoveAll:
            return ask(slot, "Remove all pairings?", kRemoveBody, kYesNo, kOnNo, false);
        case Slot::Status:
        case Slot::Count:
            break;
    }
    return Transition{};
}

Transition BluetoothPlan::slot_confirmed(SlotId slot, RowIndex choice) {
    TASTY_SEAT_BODY(BluetoothPlan);
    (void)slot;
    if (choice == kYes)
        (void)send(infra::make<Request>(Request::BtRemoveAll{}));
    return Transition::pop();
}

}
