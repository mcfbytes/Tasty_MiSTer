// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

namespace mister::hal::ocram {

struct Nonce {
    std::uint16_t v = 1;
    static constexpr std::uint16_t kMax = 0xFFFFu;

    [[nodiscard]] static constexpr Nonce minted(std::uint32_t sequence) noexcept {
        const auto low = static_cast<std::uint16_t>(sequence);
        return Nonce{low != 0u ? low : std::uint16_t{1}};
    }

    [[nodiscard]] static constexpr std::optional<Nonce> of(std::uint32_t word) noexcept {
        const auto low = static_cast<std::uint16_t>(word);
        if (low == 0u)
            return std::nullopt;
        return Nonce{low};
    }

    [[nodiscard]] constexpr Nonce next() const noexcept {
        return Nonce{static_cast<std::uint16_t>(v == kMax ? 1u : static_cast<unsigned>(v) + 1u)};
    }

    [[nodiscard]] constexpr std::uint16_t word() const noexcept { return v; }

    friend constexpr bool operator==(Nonce, Nonce) = default;
};

static_assert(sizeof(Nonce) == 2);

}
