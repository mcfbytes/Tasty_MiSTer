// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "infra/error.h"

namespace mister::os {

class Bssid {
public:
    static constexpr std::size_t kOctets = 6;
    static constexpr std::size_t kTextChars = 17;
    static constexpr std::size_t kSepChars = kOctets - 1;
    static constexpr std::size_t kCharsPerOctet = 3;
    static constexpr char kSep = ':';
    static constexpr int kNibbleBits = 4;
    static constexpr int kNibbleMask = 0x0F;

    [[nodiscard]] static Ex<Bssid> parse(std::string_view text) noexcept {
        if (text.size() != kTextChars)
            return std::unexpected(invalid());
        Bssid out;
        for (std::size_t i = 0; i < kOctets; ++i) {
            const std::size_t at = i * kCharsPerOctet;
            const int hi = digit(text[at]);
            const int lo = digit(text[at + 1]);
            if (hi < 0 || lo < 0)
                return std::unexpected(invalid());
            if (i < kSepChars && text[at + 2] != kSep)
                return std::unexpected(invalid());
            out.octets_[i] = static_cast<std::uint8_t>((hi << kNibbleBits) | lo);
        }
        return out;
    }

    class Text {
    public:
        [[nodiscard]] std::string_view view() const noexcept { return {text_.data(), length_}; }

        [[nodiscard]] const char* c_str() const noexcept { return text_.data(); }

    private:
        friend class Bssid;
        std::array<char, kTextChars + 1> text_{};
        std::size_t length_ = 0;
    };

    [[nodiscard]] Text format() const noexcept {
        Text out;
        constexpr char kDigits[] = "0123456789ABCDEF";
        std::size_t at = 0;
        for (std::size_t i = 0; i < kOctets; ++i) {
            const std::uint8_t octet = octets_[i];
            out.text_[at++] = kDigits[octet >> kNibbleBits];
            out.text_[at++] = kDigits[octet & kNibbleMask];
            if (i < kSepChars)
                out.text_[at++] = kSep;
        }
        out.text_[at] = '\0';
        out.length_ = at;
        return out;
    }

    friend constexpr bool operator==(const Bssid&, const Bssid&) = default;

private:
    static constexpr int kAlphaOffset = 10;
    static constexpr int kBadDigit = -1;

    [[nodiscard]] static constexpr int digit(char c) noexcept {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + kAlphaOffset;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + kAlphaOffset;
        return kBadDigit;
    }

    [[nodiscard]] static constexpr Error invalid() noexcept {
        return Error{Errc::bad_format, ERR_SITE(), 0};
    }

    std::array<std::uint8_t, kOctets> octets_{};
};

static_assert(std::is_trivially_copyable_v<Bssid>);
static_assert(std::has_unique_object_representations_v<Bssid>);
static_assert(sizeof(Bssid) == Bssid::kOctets);

}
