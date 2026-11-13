// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>
#include <utility>

#include "infra/decimal_text.h"

namespace mister::cores {

struct LadderPc {
    static constexpr std::uint8_t kDone = 0xFF;
    std::uint8_t v = 0;

    template <class Rung>
        requires std::is_enum_v<Rung> && std::is_same_v<std::underlying_type_t<Rung>, std::uint8_t>
    [[nodiscard]] static constexpr LadderPc of(Rung r) noexcept {
        return LadderPc{std::to_underlying(r)};
    }

    [[nodiscard]] static constexpr LadderPc done() noexcept { return LadderPc{kDone}; }

    [[nodiscard]] constexpr bool is_done() const noexcept { return v == kDone; }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(LadderPc, LadderPc) = default;
};

}
