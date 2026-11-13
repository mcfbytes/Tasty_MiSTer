// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/wifi_networks_plan.h"

#include <cstring>

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

enum class Slot : std::uint8_t { Status, Count };
constexpr SlotRange kNetworks = SlotRange::after_named<Slot>(
    RowCount::of(svc::WifiScanView::kRows)
);
constexpr std::string_view kHidden = "(hidden)";

infra::OptRef<const svc::WifiScanViewRow> network_at(const ConnectViews& views, SlotId slot) {
    const std::optional<std::size_t> at = kNetworks.subscript_of(slot);
    if (!views.scan || !at)
        return std::nullopt;
    const auto& row = views.scan->rows[*at];
    if (!row)
        return {};
    return infra::OptRef<const svc::WifiScanViewRow>{*row};
}

}

WifiNetworksPlan::WifiNetworksPlan(const HdPlanInputs& in)
    : ConnectSlotPlan(in, kNetworks.total()) {
    TASTY_SEAT_BODY(WifiNetworksPlan);
}

PageId WifiNetworksPlan::id() const {
    TASTY_SEAT_BODY(WifiNetworksPlan);
    return PageId::WifiNetworks;
}

std::string_view WifiNetworksPlan::title() const {
    TASTY_SEAT_BODY(WifiNetworksPlan);
    return "Wi-Fi networks";
}

bool WifiNetworksPlan::remembers_focus() const noexcept {
    TASTY_SEAT_BODY(WifiNetworksPlan);
    return false;
}

ConnectDemand WifiNetworksPlan::connect_demand() const {
    TASTY_SEAT_BODY(WifiNetworksPlan);
    return ConnectDemand::wifi_page();
}

bool WifiNetworksPlan::slot_shown(SlotId slot) const {
    TASTY_SEAT_BODY(WifiNetworksPlan);
    if (slot.is(Slot::Status))
        return true;
    infra::OptRef<const svc::WifiScanViewRow> row = network_at(views(), slot);
    return row.has_value() && row->security != os::WifiSecurity::Unsupported;
}

app::PageDescription::Row WifiNetworksPlan::slot_describe(SlotId slot) const {
    TASTY_SEAT_BODY(WifiNetworksPlan);
    if (slot.is(Slot::Status))
        return connect_row(RowKind::Value, "Wi-Fi", wifi_cell(views().wifi).view());
    infra::OptRef<const svc::WifiScanViewRow> row = network_at(views(), slot);
    if (!row)
        return connect_row(RowKind::Action, kHidden);
    const os::Ssid::Display shown = row->ssid.display();
    return connect_row(
        RowKind::Value,
        shown.view().empty() ? kHidden : shown.view(),
        scan_cell(*row).view()
    );
}

Transition WifiNetworksPlan::slot_select(SlotId slot) {
    TASTY_SEAT_BODY(WifiNetworksPlan);
    if (slot.is(Slot::Status)) {
        (void)send(infra::make<Request>(Request::WifiScan{}));
        return Transition{};
    }
    infra::OptRef<const svc::WifiScanViewRow> row = network_at(views(), slot);
    if (!row)
        return Transition{};
    asked_ssid_ = row->ssid;
    asked_security_ = row->security;
    if (row->security == os::WifiSecurity::Open) {
        Request::WifiJoin join{};
        join.security = os::WifiSecurity::Open;
        join.ssid = row->ssid;
        (void)send(infra::make<Request>(join));
        return Transition{};
    }
    scratch_.assign(row->ssid.display().view());
    return request_text(slot, TextEntryKind::Passphrase, scratch_, false);
}

Transition WifiNetworksPlan::slot_entered(SlotId slot, const TextEntryResult& result) {
    TASTY_SEAT_BODY(WifiNetworksPlan);
    (void)slot;
    auto pass = infra::as<TextEntryResult::Passphrase>(result);
    if (!pass)
        return Transition::pop();
    Request::WifiJoin join{};
    join.security = asked_security_;
    join.ssid = asked_ssid_;
    join.pass = pass->value;
    Request request = infra::make<Request>(join);
    const bool sent = send(request);
    explicit_bzero(&request, sizeof(request));
    join.pass.scrub();
    pass->value.scrub();

    return sent ? Transition::pop() : Transition{};
}

}
