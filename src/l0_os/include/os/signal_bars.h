// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "infra/seat.h"

namespace mister::os {

class SignalBars {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::uint8_t kMax = 4;

    [[nodiscard]] static constexpr SignalBars of(std::uint8_t filled) noexcept {
        return SignalBars{filled > kMax ? kMax : filled};
    }

    [[nodiscard]] constexpr std::uint8_t filled() const noexcept { return filled_; }

    class Text {
    public:
        [[nodiscard]] std::string_view view() const noexcept { return {glyphs_.data(), kMax}; }

    private:
        friend class SignalBars;
        std::array<char, kMax> glyphs_{};
    };

    [[nodiscard]] constexpr Text text() const noexcept {
        Text out;
        for (std::uint8_t i = 0; i < kMax; ++i)
            out.glyphs_[i] = i < filled_ ? kFilled : kEmpty;
        return out;
    }

    friend constexpr bool operator==(SignalBars, SignalBars) = default;

private:
    static constexpr char kFilled = '#';
    static constexpr char kEmpty = '.';

    constexpr explicit SignalBars(std::uint8_t filled) noexcept : filled_(filled) {}

    std::uint8_t filled_ = 0;
};

static_assert(std::is_trivially_copyable_v<SignalBars>);
static_assert(std::has_unique_object_representations_v<SignalBars>);

}
