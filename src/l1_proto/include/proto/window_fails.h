// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/decimal_text.h"
#include "infra/enum_set.h"
#include "infra/message_sum.h"

namespace mister::proto {

struct WindowFails {
private:
    enum class WindowSlot : std::uint8_t {
        First,
        Second,
        Third,
        Fourth,
        Fifth,
        Sixth,
        Seventh,
        Eighth,
        kCount,
    };
    using Bits = infra::EnumSet<WindowSlot, std::uint8_t>;

public:
    constexpr WindowFails() noexcept = default;

    constexpr explicit WindowFails(std::uint8_t bits) noexcept : bits_(Bits::from_bits(bits)) {}

    template <class E>
        requires std::is_enum_v<E>
    [[nodiscard]] constexpr bool contains(E w) const noexcept {
        static_assert(infra::ordinal(E::kCount) <= 8, "WindowFails carries at most eight windows");
        return bits_.contains(slot_of(w));
    }

    template <class E>
        requires std::is_enum_v<E>
    [[nodiscard]] constexpr WindowFails with(E w) const noexcept {
        static_assert(infra::ordinal(E::kCount) <= 8, "WindowFails carries at most eight windows");
        WindowFails out = *this;
        out.bits_.insert(slot_of(w));
        return out;
    }

    [[nodiscard]] infra::DecimalText text() const noexcept {
        return infra::DecimalText{bits_.bits()};
    }

    friend constexpr bool operator==(WindowFails, WindowFails) = default;

private:
    template <class E>
        requires std::is_enum_v<E>
    [[nodiscard]] static constexpr WindowSlot slot_of(E w) noexcept {
        return static_cast<WindowSlot>(infra::ordinal(w));
    }

    Bits bits_{};
};

static_assert(sizeof(WindowFails) == 1);

}
