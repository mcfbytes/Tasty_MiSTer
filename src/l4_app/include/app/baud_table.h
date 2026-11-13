// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

#include "cores/types.h"

namespace mister::app {

struct BaudTable {

    std::array<cores::BaudRate, 13> speed{};
    friend constexpr bool operator==(const BaudTable&, const BaudTable&) = default;

    [[nodiscard]] static BaudTable parse(std::string_view token) noexcept;

    [[nodiscard]] constexpr bool empty() const noexcept {
        return speed.front() == cores::BaudRate{};
    }

    [[nodiscard]] constexpr cores::BaudRate first() const noexcept { return speed.front(); }

    [[nodiscard]] constexpr BaudTable seeded(cores::BaudRate b) const noexcept {
        BaudTable out = *this;
        out.speed.front() = b;
        return out;
    }
};

}
