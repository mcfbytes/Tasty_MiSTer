// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/ipv4_addr.h"

#include <string_view>

#include "infra/decimal_text.h"

namespace mister::os {
namespace {

constexpr char kSep = '.';
constexpr char kZero = '0';
constexpr char kNine = '9';

[[nodiscard]] Error invalid() noexcept {
    return Error{Errc::bad_format, ERR_SITE(), 0};
}

}

Ex<Ipv4Addr> Ipv4Addr::parse(std::string_view text) noexcept {
    constexpr unsigned kRadix = 10;
    constexpr unsigned kMaxOctet = 255;
    constexpr std::size_t kMaxDigitsPerOctet = 3;
    if (text.size() > kMaxTextChars)
        return std::unexpected(invalid());
    Ipv4Addr out;
    std::size_t octet = 0;
    std::size_t digits = 0;
    unsigned value = 0;
    for (const char c : text) {
        if (c == kSep) {
            if (digits == 0 || octet + 1 >= kOctets)
                return std::unexpected(invalid());
            out.octets_[octet++] = static_cast<std::uint8_t>(value);
            value = 0;
            digits = 0;
            continue;
        }
        if (c < kZero || c > kNine)
            return std::unexpected(invalid());
        if (digits == kMaxDigitsPerOctet)
            return std::unexpected(invalid());
        value = value * kRadix + static_cast<unsigned>(c - kZero);
        if (value > kMaxOctet)
            return std::unexpected(invalid());
        ++digits;
    }
    if (digits == 0 || octet + 1 != kOctets)
        return std::unexpected(invalid());
    out.octets_[octet] = static_cast<std::uint8_t>(value);
    return out;
}

bool Ipv4Addr::is_link_local() const noexcept {
    return octets_[0] == kLinkLocalFirst && octets_[1] == kLinkLocalSecond;
}

Ipv4Addr::Text Ipv4Addr::text() const noexcept {
    Text out;
    std::size_t at = 0;
    for (std::size_t i = 0; i < kOctets; ++i) {
        if (i != 0)
            out.text_[at++] = kSep;
        const infra::DecimalText digits{octets_[i]};
        for (const char c : digits.view())
            out.text_[at++] = c;
    }
    out.text_[at] = '\0';
    out.length_ = at;
    return out;
}

}
