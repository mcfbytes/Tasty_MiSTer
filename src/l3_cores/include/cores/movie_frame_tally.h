// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/decimal_text.h"

namespace mister::cores {

struct MovieFrameTally {
    struct Count {
        std::uint32_t v = 0;

        [[nodiscard]] constexpr Count next() const noexcept { return Count{v + 1}; }

        [[nodiscard]] constexpr bool reached(Count cap) const noexcept { return v >= cap.v; }

        [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

        [[nodiscard]] constexpr std::uint32_t printed() const noexcept { return v; }

        friend constexpr bool operator==(Count, Count) = default;
    };

    Count frames{};
    bool capped = false;
};

inline constexpr MovieFrameTally::Count kMovieFrameCap{1u << 20};

}
