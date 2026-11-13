// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/message_sum.h"
#include "ui/network_plan.h"

#include "ui/connect_text.h"

namespace mister::ui {
namespace {

using RowKind = app::PageDescription::RowKind;

enum class Slot : std::uint8_t { Wifi, Saved, Advanced, Count };

}

NetworkPlan::NetworkPlan(const HdPlanInputs& in) : ConnectSlotPlan(in, named_rows<Slot>()) {
    TASTY_SEAT_BODY(NetworkPlan);
}

PageId NetworkPlan::id() const {
    TASTY_SEAT_BODY(NetworkPlan);
    return PageId::Network;
}

std::string_view NetworkPlan::title() const {
    TASTY_SEAT_BODY(NetworkPlan);
    return "Network";
}

RowCount NetworkPlan::pinned() const noexcept {
    TASTY_SEAT_BODY(NetworkPlan);
    return kOneRow;
}

ConnectDemand NetworkPlan::connect_demand() const {
    TASTY_SEAT_BODY(NetworkPlan);
    return ConnectDemand::wifi_page();
}

bool NetworkPlan::slot_shown(SlotId slot) const {
    TASTY_SEAT_BODY(NetworkPlan);
    if (!slot.is(Slot::Saved))
        return true;
    return views().saved && views().saved->rows[0].has_value();
}

app::PageDescription::Row NetworkPlan::slot_describe(SlotId slot) const {
    TASTY_SEAT_BODY(NetworkPlan);
    switch (slot.named<Slot>()) {
        case Slot::Wifi:
            return connect_row(RowKind::Value, "Wi-Fi", wifi_cell(views().wifi).view());
        case Slot::Saved:
            return connect_row(RowKind::Submenu, "Saved networks");
        case Slot::Advanced:
        case Slot::Count:
            break;
    }
    return connect_row(RowKind::Submenu, "Advanced");
}

Transition NetworkPlan::slot_select(SlotId slot) {
    TASTY_SEAT_BODY(NetworkPlan);
    switch (slot.named<Slot>()) {
        case Slot::Wifi:
            if (!views().wifi || views().wifi->presence == svc::WifiPresence::NoAdapter) {
                return Transition{};
            }
            return Transition::push(PageId::WifiNetworks);
        case Slot::Saved:
            return Transition::push(PageId::WifiSaved);
        case Slot::Advanced:
        case Slot::Count:
            break;
    }
    return Transition::push(PageId::WifiAdvanced);
}

}
