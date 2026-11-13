// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "infra/error.h"

namespace mister::os {

class CountryCode {
public:
    static constexpr std::size_t kLength = 2;
    static constexpr char kUpperFirst = 'A';
    static constexpr char kUpperLast = 'Z';

    [[nodiscard]] static constexpr Ex<CountryCode> parse(std::string_view code) noexcept {
        if (code.size() != kLength)
            return std::unexpected(invalid());
        CountryCode out;
        for (std::size_t i = 0; i < kLength; ++i) {
            const char c = code[i];
            if (c < kUpperFirst || c > kUpperLast)
                return std::unexpected(invalid());
            out.letters_[i] = c;
        }
        return out;
    }

    [[nodiscard]] constexpr std::string_view code() const noexcept {
        return {letters_.data(), kLength};
    }

    friend constexpr bool operator==(const CountryCode&, const CountryCode&) = default;

private:
    [[nodiscard]] static constexpr Error invalid() noexcept {
        return Error{Errc::bad_format, ERR_SITE(), 0};
    }

    std::array<char, kLength> letters_{};
};

static_assert(std::is_trivially_copyable_v<CountryCode>);
static_assert(std::has_unique_object_representations_v<CountryCode>);
static_assert(sizeof(CountryCode) == CountryCode::kLength);

}
