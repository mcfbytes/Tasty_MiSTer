// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstdint>

#include "infra/error.h"

namespace mister::hal {

struct PadEntries {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr PadEntries minus(PadEntries used) const noexcept {
        return PadEntries{static_cast<std::uint8_t>(v - used.v)};
    }

    [[nodiscard]] constexpr infra::Error refusal(
        infra::Errc code,
        infra::ErrorSite at
    ) const noexcept {
        return infra::Error{code, at, v};
    }

    friend constexpr bool operator==(PadEntries, PadEntries) = default;
    friend constexpr auto operator<=>(PadEntries, PadEntries) = default;
};

}
