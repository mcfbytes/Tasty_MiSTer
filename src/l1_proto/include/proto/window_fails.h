// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/decimal_text.h"
#include "infra/message_sum.h"

namespace mister::proto {

struct WindowFails {
    std::uint8_t v = 0;

    template <class E>
        requires std::is_enum_v<E>
    [[nodiscard]] constexpr bool has(E w) const noexcept {
        static_assert(infra::ordinal(E::kCount) <= 8, "WindowFails carries at most eight windows");
        return (v & (1u << infra::ordinal(w))) != 0;
    }

    template <class E>
        requires std::is_enum_v<E>
    [[nodiscard]] constexpr WindowFails with(E w) const noexcept {
        static_assert(infra::ordinal(E::kCount) <= 8, "WindowFails carries at most eight windows");
        return WindowFails{static_cast<std::uint8_t>(v | (1u << infra::ordinal(w)))};
    }

    [[nodiscard]] infra::DecimalText text() const noexcept {
        return infra::DecimalText{static_cast<std::uint16_t>(v)};
    }

    friend constexpr bool operator==(WindowFails, WindowFails) = default;
};

static_assert(sizeof(WindowFails) == 1);

}
