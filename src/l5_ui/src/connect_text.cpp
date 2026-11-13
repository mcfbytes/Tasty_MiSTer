// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/connect_text.h"

#include "os/wifi_security.h"
#include "svc/bt_activity.h"
#include "svc/bt_presence.h"
#include "svc/wifi_link.h"
#include "svc/wifi_presence.h"

namespace mister::ui {

using RowKind = app::PageDescription::RowKind;

namespace {

constexpr std::string_view kChecking = "Checking...";
constexpr std::string_view kGap = " ";

ConnectCell cell_of(std::string_view text) {
    ConnectCell out{};
    (void)out.assign(text);
    return out;
}

std::string_view security_text(os::WifiSecurity security) {
    switch (security) {
        case os::WifiSecurity::Open:
            return "open";
        case os::WifiSecurity::Psk:
            return "WPA2";
        case os::WifiSecurity::PskSae:
            return "WPA2/3";
        case os::WifiSecurity::Sae:
            return "WPA3";
        case os::WifiSecurity::Unsupported:
            return "n/a";
    }
    return {};
}

}

app::PageDescription::Row connect_row(
    RowKind kind,
    std::string_view label,
    std::string_view value
) {
    app::PageDescription::Row row{};
    row.kind = kind;
    (void)row.label.assign(label);
    (void)row.value.assign(value);
    return row;
}

std::string_view wifi_failure_text(svc::WifiFailure failure) {
    switch (failure) {
        case svc::WifiFailure::None:
            return {};
        case svc::WifiFailure::SupplicantWontStart:
            return "Wi-Fi won't start";
        case svc::WifiFailure::ConfUnreadable:
            return "Config unreadable";
        case svc::WifiFailure::WrongKey:
            return "Wrong password";
        case svc::WifiFailure::NotFound:
            return "Network not found";
        case svc::WifiFailure::Rejected:
            return "Rejected";
    }
    return {};
}

std::string_view bt_failure_text(svc::BtFailure failure) {
    switch (failure) {
        case svc::BtFailure::None:
            return {};
        case svc::BtFailure::AuthenticationFailed:
            return "Pairing failed";
        case svc::BtFailure::AuthenticationRejected:
            return "Pairing rejected";
        case svc::BtFailure::AuthenticationCanceled:
            return "Pairing canceled";
        case svc::BtFailure::ConnectionAttemptFailed:
            return "Connect failed";
        case svc::BtFailure::PairTimedOut:
            return "Pairing timed out";
        case svc::BtFailure::Failed:
            return "Failed";
    }
    return {};
}

ConnectCell wifi_cell(const std::optional<svc::WifiView>& view) {
    if (!view)
        return cell_of(kChecking);
    switch (view->presence) {
        case svc::WifiPresence::NoAdapter:
            return cell_of("No adapter");
        case svc::WifiPresence::NoSupplicant:
            return cell_of("Starting Wi-Fi...");
        case svc::WifiPresence::Ready:
            break;
    }
    if (view->failure != svc::WifiFailure::None)
        return cell_of(wifi_failure_text(view->failure));
    switch (view->link) {
        case svc::WifiLink::Connected: {
            ConnectCell out{};
            if (view->ssid)
                (void)out.assign(view->ssid->display().view());
            if (view->signal) {
                (void)out.append(kGap);
                (void)out.append(view->signal->bars().text().view());
            }
            return out.empty() ? cell_of("Connected") : out;
        }
        case svc::WifiLink::Associating:
        case svc::WifiLink::Handshake:
            return cell_of("Connecting...");
        case svc::WifiLink::Scanning:
            return cell_of("Scanning...");
        case svc::WifiLink::Disabled:
            return cell_of("Off");
        case svc::WifiLink::Disconnected:
            return cell_of("Not connected");
    }
    return {};
}

ConnectCell bt_cell(const std::optional<svc::BtView>& view) {
    if (!view)
        return cell_of(kChecking);
    switch (view->presence) {
        case svc::BtPresence::NoBus:
            return cell_of("Bluetooth is off");
        case svc::BtPresence::NoDaemon:
            return cell_of("Not running");
        case svc::BtPresence::NoAdapter:
            return cell_of("No adapter");
        case svc::BtPresence::Off:
            return cell_of("Off");
        case svc::BtPresence::Ready:
            break;
    }
    switch (view->activity.kind()) {
        case svc::BtActivity::Kind::Discovering:
            return cell_of("Searching...");
        case svc::BtActivity::Kind::Pairing:
            return cell_of("Pairing...");
        case svc::BtActivity::Kind::Connecting:
            return cell_of("Connecting...");
        case svc::BtActivity::Kind::Idle:
            break;
    }
    if (view->failure != svc::BtFailure::None)
        return cell_of(bt_failure_text(view->failure));
    return cell_of("Ready");
}

ConnectCell scan_cell(const svc::WifiScanViewRow& row) {
    ConnectCell out{};
    (void)out.assign(row.signal.bars().text().view());
    (void)out.append(kGap);
    (void)out.append(row.saved ? std::string_view{"saved"} : security_text(row.security));
    return out;
}

}
