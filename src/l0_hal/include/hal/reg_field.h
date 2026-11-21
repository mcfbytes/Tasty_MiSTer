// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstdint>

namespace mister::hal {

struct RegWord;

template <std::unsigned_integral Rep>
struct RegField {

    struct Value {
        Rep v = 0;
        friend constexpr bool operator==(Value, Value) = default;
    };

    std::uint8_t lsb = 0;
    Rep mask = 0;

    [[nodiscard]] constexpr Rep word_mask() const noexcept { return static_cast<Rep>(mask << lsb); }

    [[nodiscard]] constexpr Rep get(Rep word) const noexcept {
        return static_cast<Rep>((word >> lsb) & mask);
    }

    [[nodiscard]] constexpr Value get(RegWord word) const noexcept
        requires std::same_as<Rep, std::uint32_t>;

    [[nodiscard]] constexpr Value get(Value outer) const noexcept { return Value{get(outer.v)}; }

    [[nodiscard]] constexpr Rep set(Rep word, Value value) const noexcept {
        const Rep bits = word_mask();
        return static_cast<Rep>(
            (word & static_cast<Rep>(~bits)) | static_cast<Rep>((value.v & mask) << lsb)
        );
    }

    [[nodiscard]] constexpr bool test(Rep word) const noexcept {
        return (word & word_mask()) != Rep{0};
    }

    [[nodiscard]] constexpr bool test(Value outer) const noexcept { return test(outer.v); }

    friend constexpr bool operator==(RegField, RegField) = default;
};

}
