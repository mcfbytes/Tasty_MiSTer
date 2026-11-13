// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/ssid.h"

#include <array>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>

#include "infra/byte_view.h"
#include "os/hex_digit.h"

namespace mister::os {
namespace {

constexpr char kEscapeChar = 0x1b;
constexpr char kNewlineChar = 0x0a;
constexpr char kReturnChar = 0x0d;
constexpr char kTabChar = 0x09;
constexpr char kBackslash = '\\';
constexpr char kDQuote = '"';
constexpr char kHexMark = 'x';

constexpr std::byte kAsciiLimit{0x80};
constexpr std::byte kTwoByteFirst{0xC2};
constexpr std::byte kThreeByteFirst{0xE0};
constexpr std::byte kFourByteFirst{0xF0};
constexpr std::byte kLeadMax{0xF4};
constexpr std::byte kContFirst{0x80};
constexpr std::byte kContLast{0xBF};

[[nodiscard]] Error bad_format() noexcept {
    return Error{Errc::bad_format, ERR_SITE(), 0};
}

[[nodiscard]] bool utf8_valid(std::string_view text) noexcept {
    constexpr std::size_t kTwoByteExtra = 1;
    constexpr std::size_t kThreeByteExtra = 2;
    constexpr std::size_t kFourByteExtra = 3;
    const std::span<const std::byte> bytes = infra::text_bytes(text);
    std::size_t i = 0;
    while (i < bytes.size()) {
        const std::byte lead = bytes[i];
        if (lead < kAsciiLimit) {
            ++i;
            continue;
        }
        std::size_t extra = 0;
        bool known = false;
        if (lead >= kTwoByteFirst && lead < kThreeByteFirst) {
            extra = kTwoByteExtra;
            known = true;
        } else if (lead >= kThreeByteFirst && lead < kFourByteFirst) {
            extra = kThreeByteExtra;
            known = true;
        } else if (lead >= kFourByteFirst && lead <= kLeadMax) {
            extra = kFourByteExtra;
            known = true;
        }
        if (!known || i + extra >= bytes.size())
            return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            const std::byte cont = bytes[i + k];
            if (cont < kContFirst || cont > kContLast)
                return false;
        }
        i += extra + 1;
    }
    return true;
}

}

Ex<Ssid> Ssid::parse(std::string_view octets) noexcept {
    if (octets.size() > kMaxBytes)
        return std::unexpected(bad_format());
    Ssid out;
    if (!octets.empty())
        std::memcpy(out.octets_.data(), octets.data(), octets.size());
    out.length_ = static_cast<std::uint8_t>(octets.size());
    return out;
}

Ex<Ssid> Ssid::from_hex(std::string_view hex) noexcept {
    if (hex.size() % kHexDigitsPerByte != 0 || hex.size() > kMaxHexChars) {
        return std::unexpected(bad_format());
    }
    Ssid out;
    const std::size_t octets = hex.size() / kHexDigitsPerByte;
    for (std::size_t i = 0; i < octets; ++i) {
        const auto hi = HexDigit::of(hex[i * kHexDigitsPerByte]);
        const auto lo = HexDigit::of(hex[i * kHexDigitsPerByte + 1]);
        if (!hi || !lo)
            return std::unexpected(bad_format());
        out.octets_[i] = HexDigit::octet(*hi, *lo);
    }
    out.length_ = static_cast<std::uint8_t>(octets);
    return out;
}

Ex<Ssid> Ssid::from_escaped(std::string_view escaped) noexcept {
    constexpr std::size_t kEscapeHexDigits = 2;
    std::array<char, kMaxBytes> raw{};
    std::size_t n = 0;
    for (std::size_t i = 0; i < escaped.size(); ++i) {
        const char c = escaped[i];
        if (c != kBackslash) {
            if (n == kMaxBytes)
                return std::unexpected(bad_format());
            raw[n++] = c;
            continue;
        }
        if (++i == escaped.size())
            return std::unexpected(bad_format());
        char decoded = 0;
        switch (escaped[i]) {
            case '\\':
                decoded = kBackslash;
                break;
            case '"':
                decoded = kDQuote;
                break;
            case 'e':
                decoded = kEscapeChar;
                break;
            case 'n':
                decoded = kNewlineChar;
                break;
            case 'r':
                decoded = kReturnChar;
                break;
            case 't':
                decoded = kTabChar;
                break;
            case 'x': {
                if (i + kEscapeHexDigits >= escaped.size())
                    return std::unexpected(bad_format());
                const auto hi = HexDigit::of(escaped[i + 1]);
                const auto lo = HexDigit::of(escaped[i + 2]);
                if (!hi || !lo)
                    return std::unexpected(bad_format());
                decoded = HexDigit::character(*hi, *lo);
                i += kEscapeHexDigits;
                break;
            }
            default:
                return std::unexpected(bad_format());
        }
        if (n == kMaxBytes)
            return std::unexpected(bad_format());
        raw[n++] = decoded;
    }
    return parse(std::string_view{raw.data(), n});
}

Ssid::Hex Ssid::to_hex() const noexcept {
    Hex out;
    constexpr char kDigits[] = "0123456789abcdef";
    constexpr unsigned kNibbleBits = 4;
    constexpr unsigned kNibbleMask = 0x0F;
    std::size_t at = 0;
    for (std::size_t i = 0; i < length_; ++i) {
        const unsigned octet = std::to_integer<unsigned>(octets_[i]);
        out.digits_[at++] = kDigits[octet >> kNibbleBits];
        out.digits_[at++] = kDigits[octet & kNibbleMask];
    }
    out.digits_[at] = '\0';
    out.length_ = at;
    return out;
}

Ssid::Display Ssid::display() const noexcept {
    Display out;
    std::array<char, kMaxBytes> raw{};
    if (length_ != 0)
        std::memcpy(raw.data(), octets_.data(), length_);
    const std::string_view text{raw.data(), length_};
    if (utf8_valid(text)) {
        std::memcpy(out.text_.data(), text.data(), text.size());
        out.length_ = text.size();
        out.text_[out.length_] = '\0';
        return out;
    }
    constexpr char kDigits[] = "0123456789abcdef";
    constexpr unsigned kNibbleBits = 4;
    constexpr unsigned kNibbleMask = 0x0F;
    constexpr unsigned char kPrintableFirst = 0x20;
    constexpr unsigned char kPrintableLast = 0x7e;
    std::size_t at = 0;
    for (const char c : text) {
        const unsigned char b = static_cast<unsigned char>(c);
        if (b == kBackslash || b == kDQuote) {
            out.text_[at++] = kBackslash;
            out.text_[at++] = c;
        } else if (b >= kPrintableFirst && b <= kPrintableLast) {
            out.text_[at++] = c;
        } else {
            out.text_[at++] = kBackslash;
            out.text_[at++] = kHexMark;
            out.text_[at++] = kDigits[b >> kNibbleBits];
            out.text_[at++] = kDigits[b & kNibbleMask];
        }
    }
    out.text_[at] = '\0';
    out.length_ = at;
    return out;
}

}
