// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/bt_addr.h"

#include "os/hex_digit.h"

#include <optional>
#include <string_view>

namespace mister::os {
namespace {

[[nodiscard]] Error invalid() noexcept {
    return Error{Errc::bad_format, ERR_SITE(), 0};
}

}

Ex<BtAddr> BtAddr::parse(std::string_view text) noexcept {
    return parse(text, kSep);
}

Ex<BtAddr> BtAddr::parse(std::string_view text, char separator) noexcept {
    if (text.size() != kTextChars)
        return std::unexpected(invalid());
    BtAddr out;
    for (std::size_t i = 0; i < kOctets; ++i) {
        const std::size_t at = i * kCharsPerOctet;
        const auto hi = HexDigit::of(text[at]);
        const auto lo = HexDigit::of(text[at + 1]);
        if (!hi || !lo)
            return std::unexpected(invalid());
        if (i < kSepChars && text[at + 2] != separator)
            return std::unexpected(invalid());
        out.octets_[i] = std::to_integer<std::uint8_t>(HexDigit::octet(*hi, *lo));
    }
    return out;
}

BtAddr::Text BtAddr::format(char separator) const noexcept {
    Text out;
    constexpr char kDigits[] = "0123456789ABCDEF";
    std::size_t at = 0;
    for (std::size_t i = 0; i < kOctets; ++i) {
        const std::uint8_t octet = octets_[i];
        out.text_[at++] = kDigits[octet >> kNibbleBits];
        out.text_[at++] = kDigits[octet & kNibbleMask];
        if (i < kSepChars)
            out.text_[at++] = separator;
    }
    out.text_[at] = '\0';
    out.length_ = at;
    return out;
}

}
