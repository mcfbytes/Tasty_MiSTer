// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/wifi_advanced_plan.h"

#include <array>

#include "infra/message_sum.h"
#include "os/wpa_state.h"
#include "svc/connect_request.h"
#include "ui/connect_text.h"
#include "ui/hd_plan_inputs.h"

namespace mister::ui {
namespace {

using RowKind = app::PageDescription::RowKind;
using Request = svc::ConnectRequest;

enum class Slot : std::uint8_t {
    Interface,
    Bssid,
    Frequency,
    State,
    LastFailure,
    Country,
    Hidden,
    ForgetAll,
    Count,
};

constexpr std::array<std::string_view, 2> kYesNo{"Yes", "No"};
constexpr std::array<std::string_view, 1> kForgetBody{"Every saved network is removed."};
constexpr RowIndex kOnNo = RowIndex::of(1);
constexpr RowIndex kYes = RowIndex::of(0);
constexpr std::string_view kNone = "-";

}

WifiAdvancedPlan::WifiAdvancedPlan(const HdPlanInputs& in)
    : ConnectSlotPlan(in, named_rows<Slot>()) {
    TASTY_SEAT_BODY(WifiAdvancedPlan);
}

PageId WifiAdvancedPlan::id() const {
    TASTY_SEAT_BODY(WifiAdvancedPlan);
    return PageId::WifiAdvanced;
}

std::string_view WifiAdvancedPlan::title() const {
    TASTY_SEAT_BODY(WifiAdvancedPlan);
    return "Wi-Fi advanced";
}

ConnectDemand WifiAdvancedPlan::connect_demand() const {
    TASTY_SEAT_BODY(WifiAdvancedPlan);
    return ConnectDemand::wifi_page();
}

bool WifiAdvancedPlan::slot_shown(SlotId slot) const {
    TASTY_SEAT_BODY(WifiAdvancedPlan);
    if (slot.is(Slot::LastFailure)) {
        return views().wifi && views().wifi->failure != svc::WifiFailure::None;
    }
    return true;
}

app::PageDescription::Row WifiAdvancedPlan::slot_describe(SlotId slot) const {
    TASTY_SEAT_BODY(WifiAdvancedPlan);
    const std::optional<svc::WifiView>& v = views().wifi;
    switch (slot.named<Slot>()) {
        case Slot::Interface:
            return connect_row(
                RowKind::Value,
                "Interface",
                v && v->ifname ? v->ifname->view() : kNone
            );
        case Slot::Bssid:
            return connect_row(
                RowKind::Value,
                "Access point",
                v && v->bssid ? v->bssid->format().view() : kNone
            );
        case Slot::Frequency: {
            ConnectCell cell{};
            if (v && v->freq) {
                (void)cell.assign(v->freq->text().view());
                (void)cell.append(" MHz");
            } else {
                (void)cell.assign(kNone);
            }
            return connect_row(RowKind::Value, "Frequency", cell.view());
        }
        case Slot::State:
            return connect_row(
                RowKind::Value,
                "Link state",
                v ? os::wpa_state_text(v->wpa_state) : kNone
            );
        case Slot::LastFailure:
            return connect_row(
                RowKind::Value,
                "Last failure",
                v ? wifi_failure_text(v->failure) : kNone
            );
        case Slot::Country:
            return connect_row(
                RowKind::Submenu,
                "Country",
                v && v->country ? v->country->code() : kNone
            );
        case Slot::Hidden:
            return connect_row(RowKind::Action, "Join hidden network");
        case Slot::ForgetAll:
        case Slot::Count:
            break;
    }
    return connect_row(RowKind::Action, "Forget all networks");
}

Transition WifiAdvancedPlan::slot_select(SlotId slot) {
    TASTY_SEAT_BODY(WifiAdvancedPlan);
    switch (slot.named<Slot>()) {
        case Slot::Country:
            return Transition::push(PageId::CountryPick);
        case Slot::Hidden:
            return request_text(slot, TextEntryKind::HiddenSsid, "Network name", false);
        case Slot::ForgetAll:
            return ask(slot, "Forget all networks?", kForgetBody, kYesNo, kOnNo, false);
        case Slot::Interface:
        case Slot::Bssid:
        case Slot::Frequency:
        case Slot::State:
        case Slot::LastFailure:
        case Slot::Count:
            break;
    }
    return Transition{};
}

Transition WifiAdvancedPlan::slot_confirmed(SlotId slot, RowIndex choice) {
    TASTY_SEAT_BODY(WifiAdvancedPlan);
    (void)slot;
    if (choice == kYes)
        (void)send(infra::make<Request>(Request::WifiForgetAll{}));
    return Transition::pop();
}

Transition WifiAdvancedPlan::slot_entered(SlotId slot, const TextEntryResult& result) {
    TASTY_SEAT_BODY(WifiAdvancedPlan);
    (void)slot;
    if (const auto name = infra::as<TextEntryResult::Ssid>(result)) {
        hidden_ = name->value;
        scratch_.assign(name->value.display().view());
        return request_text(SlotId::of(Slot::Hidden), TextEntryKind::Passphrase, scratch_, true);
    }
    auto pass = infra::as<TextEntryResult::Passphrase>(result);
    if (!pass)
        return Transition::pop();
    Request::WifiJoin join{};
    join.security = os::WifiSecurity::Psk;
    join.hidden = true;
    join.ssid = hidden_;
    join.pass = pass->value;
    Request request = infra::make<Request>(join);
    const bool sent = send(request);
    explicit_bzero(&request, sizeof(request));
    join.pass.scrub();
    pass->value.scrub();

    return sent ? Transition::pop() : Transition{};
}

}
