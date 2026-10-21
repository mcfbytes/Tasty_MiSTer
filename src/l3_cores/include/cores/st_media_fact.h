// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/seat.h"

namespace mister::cores {

struct StMediaFact {
    TASTY_SEAT_EXEMPT(component);

    enum class Verb : std::uint8_t { Hold, NoTos, NoCart, Release, kCount };
    Verb verb = Verb::Hold;
    bool cold = false;
    std::uint8_t pad_[2]{};

    using Bytes = std::array<std::uint8_t, 4>;

    [[nodiscard]] Bytes bytes() const noexcept { return std::bit_cast<Bytes>(*this); }

    [[nodiscard]] static std::optional<StMediaFact> of(
        std::span<const std::uint8_t> facts
    ) noexcept {
        if (facts.size() != sizeof(StMediaFact))
            return std::nullopt;
        Bytes b{};
        for (std::size_t i = 0; i < b.size(); ++i)
            b[i] = facts[i];
        const auto f = std::bit_cast<StMediaFact>(b);
        if (static_cast<std::uint8_t>(f.verb) >= static_cast<std::uint8_t>(Verb::kCount))
            return std::nullopt;
        return f;
    }
};

static_assert(sizeof(StMediaFact) == 4);

}
