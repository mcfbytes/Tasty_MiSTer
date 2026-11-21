// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/connect_text.h"

#include "os/connect/wifi_security.h"
#include "svc/connect/bt_activity.h"
#include "svc/connect/bt_presence.h"
#include "svc/connect/wifi_link.h"
#include "svc/connect/wifi_presence.h"

namespace mister::ui {

using RowKind = app::hd::PageDescription::RowKind;

namespace {

constexpr std::string_view kChecking = "Checking...";
constexpr std::string_view kGap = " ";

ConnectCell cell_of(std::string_view text) {
    ConnectCell out{};
    (void)out.assign(text);
    return out;
}

std::string_view security_text(os::connect::WifiSecurity security) {
    switch (security) {
        case os::connect::WifiSecurity::Open:
            return "open";
        case os::connect::WifiSecurity::Psk:
            return "WPA2";
        case os::connect::WifiSecurity::PskSae:
            return "WPA2/3";
        case os::connect::WifiSecurity::Sae:
            return "WPA3";
        case os::connect::WifiSecurity::Unsupported:
            return "n/a";
    }
    return {};
}

}

app::hd::PageDescription::Row connect_row(
    RowKind kind,
    std::string_view label,
    std::string_view value
) {
    app::hd::PageDescription::Row row{};
    row.kind = kind;
    (void)row.label.assign(label);
    (void)row.value.assign(value);
    return row;
}

std::string_view wifi_failure_text(svc::connect::WifiFailure failure) {
    switch (failure) {
        case svc::connect::WifiFailure::None:
            return {};
        case svc::connect::WifiFailure::SupplicantWontStart:
            return "Wi-Fi won't start";
        case svc::connect::WifiFailure::ConfUnreadable:
            return "Config unreadable";
        case svc::connect::WifiFailure::WrongKey:
            return "Wrong password";
        case svc::connect::WifiFailure::NotFound:
            return "Network not found";
        case svc::connect::WifiFailure::Rejected:
            return "Rejected";
    }
    return {};
}

std::string_view bt_failure_text(svc::connect::BtFailure failure) {
    switch (failure) {
        case svc::connect::BtFailure::None:
            return {};
        case svc::connect::BtFailure::AuthenticationFailed:
            return "Pairing failed";
        case svc::connect::BtFailure::AuthenticationRejected:
            return "Pairing rejected";
        case svc::connect::BtFailure::AuthenticationCanceled:
            return "Pairing canceled";
        case svc::connect::BtFailure::ConnectionAttemptFailed:
            return "Connect failed";
        case svc::connect::BtFailure::PairTimedOut:
            return "Pairing timed out";
        case svc::connect::BtFailure::Failed:
            return "Failed";
    }
    return {};
}

ConnectCell wifi_cell(const std::optional<svc::connect::WifiView>& view) {
    if (!view)
        return cell_of(kChecking);
    switch (view->presence) {
        case svc::connect::WifiPresence::NoAdapter:
            return cell_of("No adapter");
        case svc::connect::WifiPresence::NoSupplicant:
            return cell_of("Starting Wi-Fi...");
        case svc::connect::WifiPresence::Ready:
            break;
    }
    if (view->failure != svc::connect::WifiFailure::None)
        return cell_of(wifi_failure_text(view->failure));
    switch (view->link) {
        case svc::connect::WifiLink::Connected: {
            ConnectCell out{};
            if (view->ssid)
                (void)out.assign(view->ssid->display().view());
            if (view->signal) {
                (void)out.append(kGap);
                (void)out.append(view->signal->bars().text().view());
            }
            return out.empty() ? cell_of("Connected") : out;
        }
        case svc::connect::WifiLink::Associating:
        case svc::connect::WifiLink::Handshake:
            return cell_of("Connecting...");
        case svc::connect::WifiLink::Scanning:
            return cell_of("Scanning...");
        case svc::connect::WifiLink::Disabled:
            return cell_of("Off");
        case svc::connect::WifiLink::Disconnected:
            return cell_of("Not connected");
    }
    return {};
}

ConnectCell bt_cell(const std::optional<svc::connect::BtView>& view) {
    if (!view)
        return cell_of(kChecking);
    switch (view->presence) {
        case svc::connect::BtPresence::NoBus:
            return cell_of("Bluetooth is off");
        case svc::connect::BtPresence::NoDaemon:
            return cell_of("Not running");
        case svc::connect::BtPresence::NoAdapter:
            return cell_of("No adapter");
        case svc::connect::BtPresence::Off:
            return cell_of("Off");
        case svc::connect::BtPresence::Ready:
            break;
    }
    switch (view->activity.kind()) {
        case svc::connect::BtActivity::Kind::Discovering:
            return cell_of("Searching...");
        case svc::connect::BtActivity::Kind::Pairing:
            return cell_of("Pairing...");
        case svc::connect::BtActivity::Kind::Connecting:
            return cell_of("Connecting...");
        case svc::connect::BtActivity::Kind::Idle:
            break;
    }
    if (view->failure != svc::connect::BtFailure::None)
        return cell_of(bt_failure_text(view->failure));
    return cell_of("Ready");
}

ConnectCell scan_cell(const svc::connect::WifiScanViewRow& row) {
    ConnectCell out{};
    (void)out.assign(row.signal.bars().text().view());
    (void)out.append(kGap);
    (void)out.append(row.saved ? std::string_view{"saved"} : security_text(row.security));
    return out;
}

}
