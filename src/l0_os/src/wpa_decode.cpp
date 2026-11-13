// SPDX-License-Identifier: GPL-3.0-or-later
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "infra/error.h"
#include "os/wpa_event.h"
#include "os/wpa_network_list.h"
#include "os/wpa_scan_table.h"
#include "os/wpa_state.h"
#include "os/wpa_status.h"

namespace mister::os {
namespace {

constexpr char kTab = '\t';
constexpr char kNewline = '\n';
constexpr char kCarriageReturn = '\r';
constexpr char kEquals = '=';
constexpr char kSpace = ' ';
constexpr char kQuote = '"';
constexpr char kEventOpen = '<';
constexpr char kEventClose = '>';

constexpr std::string_view kScanHeader{"bssid / frequency"};
constexpr std::string_view kNetworkHeader{"network id / ssid"};
constexpr std::string_view kAnyBssid{"any"};
constexpr std::string_view kCurrentFlag{"[CURRENT]"};
constexpr std::string_view kDisabledFlag{"[DISABLED]"};

constexpr std::string_view kStateKey{"wpa_state="};
constexpr std::string_view kSsidKey{"ssid="};
constexpr std::string_view kBssidKey{"bssid="};
constexpr std::string_view kFreqKey{"freq="};
constexpr std::string_view kIdKey{"id="};
constexpr std::string_view kIpKey{"ip_address="};
constexpr std::string_view kKeyMgmtKey{"key_mgmt="};

constexpr std::string_view kReasonKey{"reason="};
constexpr std::string_view kStatusCodeKey{"status_code="};

constexpr std::string_view kScanResults{"CTRL-EVENT-SCAN-RESULTS"};
constexpr std::string_view kScanFailed{"CTRL-EVENT-SCAN-FAILED"};
constexpr std::string_view kConnected{"CTRL-EVENT-CONNECTED"};
constexpr std::string_view kDisconnected{"CTRL-EVENT-DISCONNECTED"};
constexpr std::string_view kSsidTempDisabled{"CTRL-EVENT-SSID-TEMP-DISABLED"};
constexpr std::string_view kNetworkNotFound{"CTRL-EVENT-NETWORK-NOT-FOUND"};
constexpr std::string_view kAssocReject{"CTRL-EVENT-ASSOC-REJECT"};
constexpr std::string_view kTerminating{"CTRL-EVENT-TERMINATING"};
constexpr std::string_view kRegdomChange{"CTRL-EVENT-REGDOM-CHANGE"};

[[nodiscard]] Error bad_format() noexcept {
    return Error{Errc::bad_format, ERR_SITE(), 0};
}

[[nodiscard]] bool next_line(std::string_view& rest, std::string_view& line) noexcept {
    const std::size_t at = rest.find(kNewline);
    if (at == std::string_view::npos)
        return false;
    line = rest.substr(0, at);
    rest.remove_prefix(at + 1);
    if (!line.empty() && line.back() == kCarriageReturn)
        line.remove_suffix(1);
    return true;
}

[[nodiscard]] std::optional<std::string_view> take_column(std::string_view& line) noexcept {
    const std::size_t tab = line.find(kTab);
    if (tab == std::string_view::npos)
        return std::nullopt;
    const std::string_view column = line.substr(0, tab);
    line.remove_prefix(tab + 1);
    return column;
}

[[nodiscard]] std::optional<FreqMhz> parse_freq(std::string_view text) noexcept {
    if (text.empty())
        return std::nullopt;
    std::uint16_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return std::nullopt;
    return FreqMhz::of(value);
}

[[nodiscard]] std::optional<SignalDbm> parse_signal(std::string_view text) noexcept {
    if (text.empty())
        return std::nullopt;
    std::int16_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return std::nullopt;
    return SignalDbm::of(value);
}

template <std::integral Field>
void decode_into(std::string_view text, Field& field) noexcept {
    if (text.empty())
        return;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), field);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        field = 0;
}

[[nodiscard]] std::string_view value_after(std::string_view line, std::string_view key) noexcept {
    if (!line.starts_with(key))
        return {};
    return line.substr(key.size());
}

[[nodiscard]] std::string_view token_after(std::string_view line, std::string_view key) noexcept {
    const std::size_t at = line.find(key);
    if (at == std::string_view::npos)
        return {};
    std::string_view rest = line.substr(at + key.size());
    const std::size_t end = rest.find(kSpace);
    if (end != std::string_view::npos)
        rest = rest.substr(0, end);
    return rest;
}

[[nodiscard]] Ex<Ssid> decode_ssid(std::string_view value) noexcept {
    if (value.size() > 1 && value.front() == kQuote && value.back() == kQuote) {
        value.remove_prefix(1);
        value.remove_suffix(1);
    }
    return Ssid::from_escaped(value);
}

}

Ex<WpaScanTable> WpaScanTable::parse(std::string_view text) noexcept {
    WpaScanTable table;
    std::string_view rest = text;
    std::string_view line;
    bool first = true;
    while (next_line(rest, line)) {
        if (line.empty())
            continue;
        if (first && line.starts_with(kScanHeader)) {
            first = false;
            continue;
        }
        first = false;
        if (table.count_ >= kMaxRows)
            break;

        std::string_view rest_of_line = line;
        const auto bssid_text = take_column(rest_of_line);
        const auto freq_text = take_column(rest_of_line);
        const auto signal_text = take_column(rest_of_line);
        const auto flags_text = take_column(rest_of_line);
        if (!bssid_text || !freq_text || !signal_text || !flags_text) {
            return std::unexpected(bad_format());
        }
        const auto bssid = Bssid::parse(*bssid_text);
        if (!bssid)
            return std::unexpected(bssid.error());
        const auto freq = parse_freq(*freq_text);
        if (!freq)
            return std::unexpected(bad_format());
        const auto signal = parse_signal(*signal_text);
        if (!signal)
            return std::unexpected(bad_format());
        const auto ssid = Ssid::from_escaped(rest_of_line);
        if (!ssid)
            return std::unexpected(ssid.error());

        WpaScanTableRow& row = table.rows_[table.count_];
        row.bssid = *bssid;
        row.freq = *freq;
        row.signal = *signal;
        row.security = wifi_security_from_flags(*flags_text);
        row.ssid = *ssid;
        ++table.count_;
    }
    return table;
}

Ex<WpaStatus> WpaStatus::parse(std::string_view text) noexcept {
    WpaStatus out;
    std::string_view rest = text;
    std::string_view line;
    while (next_line(rest, line)) {
        if (line.empty())
            continue;
        if (line.starts_with(kStateKey)) {
            out.state_ = wpa_state_from_text(value_after(line, kStateKey));
            continue;
        }
        if (line.starts_with(kSsidKey)) {
            const auto ssid = decode_ssid(value_after(line, kSsidKey));
            if (!ssid)
                return std::unexpected(ssid.error());
            out.ssid_ = *ssid;
            continue;
        }
        if (line.starts_with(kBssidKey)) {
            const std::string_view bssid = value_after(line, kBssidKey);
            if (bssid.empty())
                continue;
            const auto parsed = Bssid::parse(bssid);
            if (!parsed)
                return std::unexpected(parsed.error());
            out.bssid_ = *parsed;
            continue;
        }
        if (line.starts_with(kFreqKey)) {
            const auto parsed = parse_freq(value_after(line, kFreqKey));
            if (!parsed)
                return std::unexpected(bad_format());
            out.freq_ = *parsed;
            continue;
        }
        if (line.starts_with(kIdKey)) {
            const auto parsed = WpaNetworkId::parse(value_after(line, kIdKey));
            if (!parsed)
                return std::unexpected(parsed.error());
            out.id_ = *parsed;
            continue;
        }
        const std::string_view ip = value_after(line, kIpKey);
        if (line.starts_with(kIpKey)) {
            if (ip.empty())
                continue;
            const auto parsed = Ipv4Addr::parse(ip);
            if (!parsed)
                return std::unexpected(parsed.error());
            out.ip_ = *parsed;
            continue;
        }
        if (line.starts_with(kKeyMgmtKey)) {
            const std::string_view value = value_after(line, kKeyMgmtKey);
            if (!value.empty())
                (void)out.key_mgmt_.assign(value);
            continue;
        }
    }
    return out;
}

Ex<WpaNetworkList> WpaNetworkList::parse(std::string_view text) noexcept {
    WpaNetworkList list;
    std::string_view rest = text;
    std::string_view line;
    bool first = true;
    while (next_line(rest, line)) {
        if (line.empty())
            continue;
        if (first && line.starts_with(kNetworkHeader)) {
            first = false;
            continue;
        }
        first = false;
        if (list.count_ >= kMaxRows)
            break;

        std::string_view rest_of_line = line;
        const auto id_text = take_column(rest_of_line);
        const auto ssid_text = take_column(rest_of_line);
        const auto bssid_text = take_column(rest_of_line);
        if (!id_text || !ssid_text || !bssid_text)
            return std::unexpected(bad_format());
        const auto id = WpaNetworkId::parse(*id_text);
        if (!id)
            return std::unexpected(id.error());
        const auto ssid = Ssid::from_escaped(*ssid_text);
        if (!ssid)
            return std::unexpected(ssid.error());

        WpaNetworkListRow& row = list.rows_[list.count_];
        row.id = *id;
        row.ssid = *ssid;
        if (*bssid_text != kAnyBssid) {
            const auto bssid = Bssid::parse(*bssid_text);
            if (!bssid)
                return std::unexpected(bssid.error());
            row.bssid = *bssid;
        }
        const std::string_view flags = rest_of_line;
        row.current = flags.find(kCurrentFlag) != std::string_view::npos;
        row.disabled = flags.find(kDisabledFlag) != std::string_view::npos;
        ++list.count_;
    }
    return list;
}

WpaEvent wpa_event_from_text(std::string_view text) noexcept {
    while (!text.empty() && (text.back() == kNewline || text.back() == kCarriageReturn)) {
        text.remove_suffix(1);
    }
    if (text.empty() || text.front() != kEventOpen) {
        return infra::make<WpaEvent>(WpaEvent::Unhandled{});
    }
    const std::size_t close = text.find(kEventClose);
    if (close == std::string_view::npos) {
        return infra::make<WpaEvent>(WpaEvent::Unhandled{});
    }
    const std::string_view body = text.substr(close + 1);
    const std::size_t space = body.find(kSpace);
    const std::string_view name = space == std::string_view::npos ? body : body.substr(0, space);

    if (name == kScanResults)
        return infra::make<WpaEvent>(WpaEvent::ScanResults{});
    if (name == kScanFailed)
        return infra::make<WpaEvent>(WpaEvent::ScanFailed{});
    if (name == kConnected)
        return infra::make<WpaEvent>(WpaEvent::Connected{});
    if (name == kDisconnected) {
        WpaEvent::Disconnected alternative{};
        decode_into(token_after(body, kReasonKey), alternative.reason);
        return infra::make<WpaEvent>(alternative);
    }
    if (name == kSsidTempDisabled) {
        WpaEvent::SsidTempDisabled alternative{};
        (void)alternative.reason.assign(token_after(body, kReasonKey));
        return infra::make<WpaEvent>(alternative);
    }
    if (name == kNetworkNotFound)
        return infra::make<WpaEvent>(WpaEvent::NetworkNotFound{});
    if (name == kAssocReject) {
        WpaEvent::AssocReject alternative{};
        decode_into(token_after(body, kStatusCodeKey), alternative.status_code);
        return infra::make<WpaEvent>(alternative);
    }
    if (name == kTerminating)
        return infra::make<WpaEvent>(WpaEvent::Terminating{});
    if (name == kRegdomChange)
        return infra::make<WpaEvent>(WpaEvent::RegdomChange{});
    return infra::make<WpaEvent>(WpaEvent::Unhandled{});
}

}
