// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include "proto/bit_field.h"
#include "proto/status_bit.h"
#include "proto/types.h"
#include "infra/endian.h"
#include "infra/seat.h"

namespace mister::proto {

struct StatusWord {
    static constexpr unsigned kWords = 8;
    std::array<std::uint16_t, kWords> words{};

    [[nodiscard]] constexpr bool get_bit(StatusBit bit) const noexcept { return bit_at(bit); }

    [[nodiscard]] constexpr bool has(StatusBit bit) const noexcept { return get_bit(bit); }

    [[nodiscard]] constexpr FieldValue field(BitField bits) const noexcept {
        std::uint32_t out = 0;
        for (unsigned i = 0; i < bits.width && i < BitField::kWordBits; ++i) {
            const auto bit = bits.start.plus(i);
            if (bit && bit_at(*bit))
                out |= 1u << i;
        }
        return FieldValue{out};
    }

    [[nodiscard]] constexpr StatusWord with(StatusBit bit, bool value) const noexcept {
        StatusWord out = *this;
        const std::size_t w = bit.word();
        const std::uint16_t mask = bit.mask();
        out.words[w] = value
            ? static_cast<std::uint16_t>(out.words[w] | mask)
            : static_cast<std::uint16_t>(out.words[w] & static_cast<std::uint16_t>(~mask));
        return out;
    }

    [[nodiscard]] static constexpr StatusWord all() noexcept {
        StatusWord w{};
        for (auto& word : w.words)
            word = static_cast<std::uint16_t>(0xFFFFu);
        return w;
    }

    using Bytes = std::array<std::uint8_t, kWords * sizeof(std::uint16_t)>;

    [[nodiscard]] static constexpr StatusWord from_bytes(
        std::span<const std::uint8_t, sizeof(Bytes)> b
    ) noexcept {
        StatusWord w{};
        for (unsigned i = 0; i < kWords; ++i)
            w.words[i] = static_cast<std::uint16_t>(b[2 * i] | (b[2 * i + 1] << 8));
        return w;
    }

    [[nodiscard]] static constexpr std::optional<StatusWord> of(
        std::span<const std::uint8_t> b
    ) noexcept {
        if (b.size() != sizeof(Bytes))
            return std::nullopt;
        return from_bytes(b.first<sizeof(Bytes)>());
    }

    [[nodiscard]] Bytes to_le_bytes() const noexcept {
        Bytes out{};
        infra::store_le_run(std::span<const std::uint16_t>(words), std::span<std::uint8_t>(out));
        return out;
    }

    friend constexpr bool operator==(const StatusWord&, const StatusWord&) = default;

private:
    [[nodiscard]] constexpr bool bit_at(StatusBit bit) const noexcept {
        return (words[bit.word()] & bit.mask()) != 0;
    }
};

constexpr FieldValue BitField::read(const StatusWord& word) const noexcept {
    return word.field(*this);
}

constexpr StatusWord BitField::write(StatusWord word, FieldValue value) const noexcept {
    const unsigned span = width < kWordBits ? width : kWordBits;
    for (unsigned i = 0; i < span; ++i) {
        if (const auto at = start.plus(i))
            word = word.with(*at, ((value.v >> i) & 1u) != 0u);
    }
    return word;
}

static_assert(sizeof(StatusWord) == 16);
static_assert(StatusBit::kCount == StatusWord::kWords * std::numeric_limits<std::uint16_t>::digits);
static_assert(StatusWord{}.with(StatusBit{17}, true).has(StatusBit{17}));
static_assert(!StatusWord{}.with(StatusBit{17}, false).has(StatusBit{17}));
static_assert(StatusWord::all().has(StatusBit{127}));

}
