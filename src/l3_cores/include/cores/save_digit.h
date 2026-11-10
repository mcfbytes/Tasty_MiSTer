// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "infra/fixed_str.h"

namespace mister::cores {

class SaveDigit {
public:
    using Text = FixedStr<4, StrFit::Clip>;

    static constexpr unsigned kMax = 0xFF;

    [[nodiscard]] static constexpr std::optional<SaveDigit> of(unsigned value) noexcept {
        if (value == 0 || value > kMax)
            return std::nullopt;
        return SaveDigit{static_cast<std::uint8_t>(value)};
    }

    [[nodiscard]] constexpr unsigned value() const noexcept { return v; }

    [[nodiscard]] Text text() const noexcept {
        constexpr std::uint8_t kBase = 10;
        char spelled[3]{};
        std::size_t n = 0;
        for (std::uint8_t rest = v; rest != 0; rest = static_cast<std::uint8_t>(rest / kBase)) {
            spelled[n++] = static_cast<char>('0' + (rest % kBase));
        }
        Text out;
        while (n-- != 0)
            (void)out.append(std::string_view{&spelled[n], 1});
        return out;
    }

    friend constexpr bool operator==(SaveDigit, SaveDigit) = default;

private:
    explicit constexpr SaveDigit(std::uint8_t digit) noexcept : v(digit) {}

    std::uint8_t v;
};

}
