// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "infra/decimal_text.h"
#include "infra/error.h"

namespace mister::os {

class PasskeyDigits {
public:
    static constexpr std::uint8_t kMax = 6;
    static constexpr std::size_t kMaxDigits = 1;
    static constexpr std::uint32_t kRadix = 10;
    static constexpr char kZero = '0';
    static constexpr char kNine = '9';

    [[nodiscard]] static constexpr Ex<PasskeyDigits> of(std::uint8_t value) noexcept {
        if (value > kMax)
            return std::unexpected(invalid());
        return PasskeyDigits{value};
    }

    [[nodiscard]] static constexpr Ex<PasskeyDigits> parse(std::string_view text) noexcept {
        if (text.empty() || text.size() > kMaxDigits)
            return std::unexpected(invalid());
        std::uint8_t value = 0;
        for (const char c : text) {
            if (c < kZero || c > kNine)
                return std::unexpected(invalid());
            const unsigned digit = static_cast<unsigned>(c - kZero);
            value = static_cast<std::uint8_t>(value * kRadix + digit);
        }
        return of(value);
    }

    [[nodiscard]] constexpr std::uint8_t value() const noexcept { return value_; }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{value_}; }

    friend constexpr bool operator==(const PasskeyDigits&, const PasskeyDigits&) = default;

private:
    [[nodiscard]] static constexpr Error invalid() noexcept {
        return Error{Errc::bad_format, ERR_SITE(), 0};
    }

    constexpr explicit PasskeyDigits(std::uint8_t value) noexcept : value_(value) {}

    std::uint8_t value_ = 0;
};

static_assert(std::is_trivially_copyable_v<PasskeyDigits>);
static_assert(std::has_unique_object_representations_v<PasskeyDigits>);
static_assert(sizeof(PasskeyDigits) == 1);

}
