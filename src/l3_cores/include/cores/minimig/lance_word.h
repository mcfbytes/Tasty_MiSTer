// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "cores/minimig/lance_span.h"
#include "infra/byte_count.h"
#include "infra/endian.h"

namespace mister::cores::minimig {

struct LanceWord {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr LanceWord of_be(
        std::span<const std::byte, sizeof(std::uint16_t)> b
    ) noexcept {
        return LanceWord{infra::load_be<std::uint16_t>(b)};
    }

    constexpr void store_be(std::span<std::byte, sizeof(std::uint16_t)> out) const noexcept {
        infra::store_be<std::uint16_t>(out, v);
    }

    constexpr void store_le(std::span<std::byte, sizeof(std::uint16_t)> out) const noexcept {
        infra::store_le<std::uint16_t>(out, v);
    }

    [[nodiscard]] constexpr bool intersects(LanceWord mask) const noexcept {
        return (v & mask.v) != 0u;
    }

    [[nodiscard]] constexpr bool contains(LanceWord mask) const noexcept {
        return (v & mask.v) == mask.v;
    }

    [[nodiscard]] constexpr LanceWord masked(LanceWord mask) const noexcept {
        return LanceWord{static_cast<std::uint16_t>(v & mask.v)};
    }

    [[nodiscard]] constexpr LanceWord with(LanceWord bits) const noexcept {
        return LanceWord{static_cast<std::uint16_t>(v | bits.v)};
    }

    [[nodiscard]] constexpr LanceWord without(LanceWord bits) const noexcept {
        return LanceWord{static_cast<std::uint16_t>(v & ~bits.v)};
    }

    [[nodiscard]] constexpr LanceSpan negated_count() const noexcept {
        constexpr std::uint32_t kCountSpan = 1u << std::numeric_limits<std::uint16_t>::digits;
        return LanceSpan{kCountSpan - v};
    }

    [[nodiscard]] static constexpr LanceWord of_length(infra::ByteCount n) noexcept {
        return LanceWord{static_cast<std::uint16_t>(n.v)};
    }

    friend constexpr bool operator==(LanceWord, LanceWord) = default;
};

static_assert(sizeof(LanceWord) == 2);

struct LanceWordPair {
    LanceWord low{};
    LanceWord high{};
    friend constexpr bool operator==(LanceWordPair, LanceWordPair) = default;
};

}
