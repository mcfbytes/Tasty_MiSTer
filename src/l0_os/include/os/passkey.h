// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "infra/decimal_text.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "os/types.h"

namespace mister::os {

class Passkey {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::uint32_t kMax = 999999;
    static constexpr std::size_t kDigits = 6;
    static constexpr std::uint32_t kRadix = 10;
    static constexpr char kZero = '0';

    [[nodiscard]] static constexpr Ex<Passkey> of(std::uint32_t value) noexcept {
        if (value > kMax)
            return std::unexpected(invalid());
        return Passkey{value};
    }

    [[nodiscard]] static constexpr Passkey zero() noexcept { return Passkey{0}; }

    [[nodiscard]] static constexpr Ex<Passkey> parse(std::string_view text) noexcept {
        if (text.empty() || text.size() > kDigits)
            return std::unexpected(invalid());
        std::uint32_t value = 0;
        for (const char c : text) {
            if (c < kZero || c > kNine)
                return std::unexpected(invalid());
            value = value * kRadix + static_cast<std::uint32_t>(c - kZero);
        }
        return of(value);
    }

    [[nodiscard]] constexpr std::uint32_t value() const noexcept { return value_; }

    [[nodiscard]] constexpr WireU32 wire() const noexcept { return WireU32{value_}; }

    class Text {
    public:
        [[nodiscard]] std::string_view view() const noexcept { return {digits_.data(), kDigits}; }

        [[nodiscard]] const char* c_str() const noexcept { return digits_.data(); }

    private:
        friend class Passkey;
        std::array<char, kDigits + 1> digits_{};
    };

    [[nodiscard]] constexpr Text format6() const noexcept {
        Text out;
        std::uint32_t value = value_;
        for (std::size_t i = kDigits; i > 0; --i) {
            out.digits_[i - 1] = static_cast<char>(kZero + static_cast<char>(value % kRadix));
            value /= kRadix;
        }
        out.digits_[kDigits] = '\0';
        return out;
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{value_}; }

    [[nodiscard]] static constexpr bool is_digit(char c) noexcept {
        return c >= kZero && c <= kNine;
    }

    friend constexpr bool operator==(const Passkey&, const Passkey&) = default;

private:
    static constexpr char kNine = '9';

    [[nodiscard]] static constexpr Error invalid() noexcept {
        return Error{Errc::bad_format, ERR_SITE(), 0};
    }

    constexpr explicit Passkey(std::uint32_t value) noexcept : value_(value) {}

    std::uint32_t value_ = 0;
};

static_assert(std::is_trivially_copyable_v<Passkey>);
static_assert(std::has_unique_object_representations_v<Passkey>);
static_assert(sizeof(Passkey) == sizeof(std::uint32_t));

}
