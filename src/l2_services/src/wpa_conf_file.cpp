// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/wpa_conf_file.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mister::svc {
namespace {

constexpr std::string_view kNetworkHeader = "network={";
constexpr std::string_view kCloseBrace = "}";
constexpr std::string_view kCountryKey = "country";
constexpr std::string_view kSsidKey = "ssid";
constexpr std::string_view kSsidValuePrefix = "    ssid=";
constexpr std::string_view kPskValuePrefix = "    psk=";
constexpr std::string_view kKeyMgmtValuePrefix = "    key_mgmt=";
constexpr std::string_view kIeee80211wValuePrefix = "    ieee80211w=";
constexpr std::string_view kScanSsidLine = "    scan_ssid=1";

constexpr char kEquals = '=';
constexpr char kSpace = ' ';
constexpr char kTab = '\t';
constexpr char kReturn = '\r';
constexpr char kNewline = '\n';
constexpr char kHash = '#';
constexpr char kBackslash = '\\';
constexpr char kOpenBrace = '{';

constexpr char kPrintableFirst = ' ';
constexpr char kPrintableLast = '~';

struct KeyValue {
    std::string_view key;
    std::string_view value;
    bool ok = false;
};

class QuotedValue {
public:
    explicit constexpr QuotedValue(std::string_view spelled) noexcept : spelled_(spelled) {}

    [[nodiscard]] static constexpr char quote() noexcept { return kQuoteMark.front(); }

    [[nodiscard]] constexpr bool wrapped() const noexcept {
        return spelled_.size() > kQuoteMark.size()
            && spelled_.starts_with(kQuoteMark)
            && spelled_.ends_with(kQuoteMark);
    }

    [[nodiscard]] constexpr bool unterminated() const noexcept {
        return spelled_.starts_with(kQuoteMark) && !wrapped();
    }

    [[nodiscard]] constexpr std::string_view inner() const noexcept {
        std::string_view inner = spelled_;
        inner.remove_prefix(kQuoteMark.size());
        inner.remove_suffix(kQuoteMark.size());
        return inner;
    }

    [[nodiscard]] static std::string spelling_of(std::span<const std::byte> bytes) {
        std::string out;

        out.reserve(bytes.size() + kQuoteMark.size() + kQuoteMark.size());
        out.push_back(kQuoteMark.front());
        for (const std::byte b : bytes)
            out.push_back(std::to_integer<char>(b));
        out.push_back(kQuoteMark.front());
        return out;
    }

private:
    static constexpr std::string_view kQuoteMark{"\""};
    std::string_view spelled_{};
};

struct ConfLine {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr ConfLine next() const noexcept {
        const std::uint32_t number = v + 1;
        return ConfLine{number};
    }

    [[nodiscard]] constexpr std::uint32_t detail() const noexcept {
        const std::uint32_t word = v;
        return word;
    }

    friend constexpr bool operator==(ConfLine, ConfLine) = default;
};

[[nodiscard]] bool is_blank(char c) noexcept {
    return c == kSpace || c == kTab || c == kReturn;
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    std::size_t first = 0;
    while (first < text.size() && is_blank(text[first]))
        ++first;
    std::size_t last = text.size();
    while (last > first && is_blank(text[last - 1]))
        --last;
    return text.substr(first, last - first);
}

[[nodiscard]] std::string_view structural(std::string_view line) noexcept {
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == QuotedValue::quote()) {
            quoted = !quoted;
            continue;
        }
        if (!quoted && c == kHash)
            return trim(line.substr(0, i));
    }
    return trim(line);
}

[[nodiscard]] KeyValue split_key_value(std::string_view structure) noexcept {
    const std::size_t eq = structure.find(kEquals);
    if (eq == std::string_view::npos)
        return {};
    return KeyValue{trim(structure.substr(0, eq)), trim(structure.substr(eq + 1)), true};
}

[[nodiscard]] bool opens_block(std::string_view structure) noexcept {
    return !structure.empty() && structure.back() == kOpenBrace;
}

[[nodiscard]] bool quoted_unterminated(std::string_view value) noexcept {
    return QuotedValue{value}.unterminated();
}

[[nodiscard]] Ex<os::Ssid> decode_ssid_value(std::string_view value) {
    const QuotedValue quoted{value};
    if (quoted.wrapped())
        return os::Ssid::parse(quoted.inner());
    return os::Ssid::from_hex(value);
}

[[nodiscard]] std::string_view ieee80211w_text(os::WifiSecurity security) noexcept {
    switch (os::wifi_security_ieee80211w(security)) {
        case os::kWifiIeee80211wOff:
            return "0";
        case os::kWifiIeee80211wOptional:
            return "1";
        case os::kWifiIeee80211wRequired:
            return "2";
    }
    return "0";
}

[[nodiscard]] bool quote_carries(std::span<const std::byte> bytes) noexcept {
    for (const std::byte b : bytes) {
        const unsigned char c = std::to_integer<unsigned char>(b);
        if (c < kPrintableFirst || c > kPrintableLast)
            return false;
        if (c == QuotedValue::quote() || c == kBackslash)
            return false;
    }
    return true;
}

[[nodiscard]] std::string ssid_text(const os::Ssid& ssid) {
    if (quote_carries(ssid.bytes()))
        return QuotedValue::spelling_of(ssid.bytes());
    return std::string{ssid.to_hex().view()};
}

[[nodiscard]] std::unexpected<Error> bad_line(ConfLine line) noexcept {
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), line.detail()});
}

[[nodiscard]] std::unexpected<Error> refused() noexcept {
    return std::unexpected(Error{Errc::contract, ERR_SITE(), 0});
}

}

Ex<WpaConfFile> WpaConfFile::parse(std::string_view text) {
    enum class Mode : std::uint8_t { Top, Network, Opaque };

    WpaConfFile out;
    Mode mode = Mode::Top;
    std::vector<std::string> block;
    ConfLine block_line{};
    os::Ssid ssid{};
    bool have_ssid = false;
    bool country_seen = false;
    ConfLine line_no{1};
    std::size_t start = 0;

    while (start < text.size()) {
        const std::size_t nl = text.find(kNewline, start);
        const bool last = nl == std::string_view::npos;
        const std::string_view raw = last ? text.substr(start) : text.substr(start, nl - start);
        const std::string_view structure = structural(raw);

        if (mode == Mode::Top) {
            if (structure == kNetworkHeader) {
                mode = Mode::Network;
                block.clear();
                block.emplace_back(raw);
                block_line = line_no;
                have_ssid = false;
                ssid = os::Ssid{};
            } else if (opens_block(structure)) {
                mode = Mode::Opaque;
                block_line = line_no;
                out.lines_.emplace_back(raw);
                out.items_.push_back(Item::of_line(Place{out.lines_.size() - 1}));
            } else if (structure == kCloseBrace) {
                return bad_line(line_no);
            } else {
                const KeyValue kv = split_key_value(structure);
                if (kv.ok && kv.key == kCountryKey) {
                    if (country_seen)
                        return bad_line(line_no);
                    country_seen = true;
                }
                out.lines_.emplace_back(raw);
                out.items_.push_back(Item::of_line(Place{out.lines_.size() - 1}));
            }
        } else if (mode == Mode::Opaque) {
            if (opens_block(structure))
                return bad_line(line_no);
            out.lines_.emplace_back(raw);
            out.items_.push_back(Item::of_line(Place{out.lines_.size() - 1}));
            if (structure == kCloseBrace)
                mode = Mode::Top;
        } else {
            if (opens_block(structure))
                return bad_line(line_no);
            if (structure == kCloseBrace) {
                if (!have_ssid)
                    return bad_line(block_line);
                block.emplace_back(raw);
                out.networks_.push_back(WpaConfFileNetwork{ssid, std::move(block)});
                out.items_.push_back(Item::of_block(Place{out.networks_.size() - 1}));
                block.clear();
                mode = Mode::Top;
            } else {
                const KeyValue kv = split_key_value(structure);
                if (kv.ok && kv.key == kSsidKey) {
                    auto parsed = decode_ssid_value(kv.value);
                    if (!parsed)
                        return bad_line(line_no);
                    if (parsed->empty())
                        return bad_line(line_no);
                    ssid = *parsed;
                    have_ssid = true;
                } else if (kv.ok && quoted_unterminated(kv.value)) {
                    return bad_line(line_no);
                }
                block.emplace_back(raw);
            }
        }

        if (last) {
            out.trailing_newline_ = false;
            break;
        }
        start = nl + 1;
        line_no = line_no.next();
    }

    if (mode != Mode::Top)
        return bad_line(block_line);
    return out;
}

Ex<void> WpaConfFile::add(
    os::Ssid ssid,
    os::WifiSecurity security,
    const os::Passphrase* passphrase,
    bool hidden
) {
    if (security == os::WifiSecurity::Unsupported || ssid.empty())
        return refused();
    const bool needs_passphrase = security != os::WifiSecurity::Open;
    if (needs_passphrase && passphrase == nullptr)
        return refused();

    remove(ssid);

    std::vector<std::string> block;
    block.emplace_back(kNetworkHeader);

    std::string ssid_line{kSsidValuePrefix};
    ssid_line.append(ssid_text(ssid));
    block.push_back(std::move(ssid_line));

    if (needs_passphrase) {
        std::string psk_line{kPskValuePrefix};
        psk_line.append(passphrase->write_quoted().view());
        block.push_back(std::move(psk_line));
    }

    std::string key_line{kKeyMgmtValuePrefix};
    key_line.append(os::wifi_security_key_mgmt(security));
    block.push_back(std::move(key_line));

    std::string w_line{kIeee80211wValuePrefix};
    w_line.append(ieee80211w_text(security));
    block.push_back(std::move(w_line));

    if (hidden)
        block.emplace_back(kScanSsidLine);
    block.emplace_back(kCloseBrace);

    const bool last_empty = !items_.empty()
        && !items_.back().is_block()
        && line_at(items_.back().at).empty();
    if (!items_.empty() && !last_empty) {
        lines_.emplace_back();
        items_.push_back(Item::of_line(Place{lines_.size() - 1}));
    }

    networks_.push_back(WpaConfFileNetwork{ssid, std::move(block)});
    items_.push_back(Item::of_block(Place{networks_.size() - 1}));
    return {};
}

void WpaConfFile::remove(os::Ssid ssid) {
    constexpr std::size_t kGone = std::numeric_limits<std::size_t>::max();

    std::vector<WpaConfFileNetwork> kept;
    std::vector<std::size_t> remap(networks_.size(), kGone);
    for (std::size_t i = 0; i < networks_.size(); ++i) {
        if (networks_[i].ssid() == ssid)
            continue;
        remap[i] = kept.size();
        kept.push_back(std::move(networks_[i]));
    }

    std::vector<Item> items;
    items.reserve(items_.size());
    for (const Item& item : items_) {
        if (!item.is_block()) {
            items.push_back(item);
            continue;
        }
        if (remap[item.at.index()] == kGone)
            continue;
        items.push_back(Item::of_block(Place{remap[item.at.index()]}));
    }

    networks_ = std::move(kept);
    items_ = std::move(items);
}

void WpaConfFile::set_country(os::CountryCode country) {
    std::string replacement{kCountryKey};
    replacement.push_back(kEquals);
    replacement.append(country.code());

    for (const Item& item : items_) {
        if (item.is_block())
            continue;
        std::string& line = line_at(item.at);
        const KeyValue kv = split_key_value(structural(line));
        if (!kv.ok || kv.key != kCountryKey)
            continue;
        if (!line.empty() && line.back() == kReturn)
            replacement.push_back(kReturn);
        line = replacement;
        return;
    }

    lines_.push_back(std::move(replacement));
    items_.insert(items_.begin(), Item::of_line(Place{lines_.size() - 1}));
}

std::string WpaConfFile::text() const {
    std::string out;
    bool any = false;
    auto put = [&out, &any](std::string_view line) {
        if (any)
            out.push_back(kNewline);
        out.append(line);
        any = true;
    };

    for (const Item& item : items_) {
        if (item.is_block()) {
            for (const std::string& line : block_at(item.at).lines())
                put(line);
        } else {
            put(line_at(item.at));
        }
    }
    if (any && trailing_newline_)
        out.push_back(kNewline);
    return out;
}

std::optional<os::CountryCode> WpaConfFile::country() const noexcept {
    for (const Item& item : items_) {
        if (item.is_block())
            continue;
        const KeyValue kv = split_key_value(structural(line_at(item.at)));
        if (!kv.ok || kv.key != kCountryKey)
            continue;
        const QuotedValue quoted{kv.value};
        const std::string_view value = quoted.wrapped() ? quoted.inner() : kv.value;
        auto code = os::CountryCode::parse(value);
        if (code)
            return *code;
        return std::nullopt;
    }
    return std::nullopt;
}

}
