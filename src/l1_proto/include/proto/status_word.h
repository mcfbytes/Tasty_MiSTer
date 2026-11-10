// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "proto/bit_field.h"
#include "proto/types.h"
#include "infra/seat.h"

namespace mister::proto {

struct StatusWord {
    static constexpr unsigned kBits = 128;
    static constexpr unsigned kBitsPerWord = 16;
    static constexpr unsigned kWords = 8;
    std::array<std::uint16_t, kWords> words{};

    [[nodiscard]] constexpr bool get_bit(StatusBit bit) const noexcept {
        if (!bit.within(kBits))
            return false;
        return bit_at(bit);
    }

    [[nodiscard]] constexpr bool has(StatusBit bit) const noexcept { return get_bit(bit); }

    [[nodiscard]] constexpr FieldValue field(BitField bits) const noexcept {
        std::uint32_t out = 0;
        unsigned i = 0;
        for (const StatusBit bit : bits.each_bit()) {
            if (i >= BitField::kWordBits)
                break;
            if (bit.within(kBits) && bit_at(bit))
                out |= 1u << i;
            ++i;
        }
        return FieldValue{out};
    }

    [[nodiscard]] constexpr StatusWord with(StatusBit bit, bool value) const noexcept {
        if (!bit.within(kBits))
            return *this;
        StatusWord out = *this;
        const std::size_t w = bit.word_index(kBitsPerWord);
        const auto mask = static_cast<std::uint16_t>(1u << bit.bit_index(kBitsPerWord));
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

    [[nodiscard]] static constexpr StatusWord from_bytes(
        std::span<const std::uint8_t, 16> b
    ) noexcept {
        StatusWord w{};
        for (unsigned i = 0; i < kWords; ++i)
            w.words[i] = static_cast<std::uint16_t>(b[2 * i] | (b[2 * i + 1] << 8));
        return w;
    }

    friend constexpr bool operator==(const StatusWord&, const StatusWord&) = default;

private:
    [[nodiscard]] constexpr bool bit_at(StatusBit bit) const noexcept {
        return ((static_cast<unsigned>(words[bit.word_index(kBitsPerWord)])
                 >> bit.bit_index(kBitsPerWord))
                & 1u)
            != 0u;
    }
};

constexpr FieldValue BitField::read(const StatusWord& word) const noexcept {
    return word.field(*this);
}

constexpr StatusWord BitField::write(StatusWord word, FieldValue value) const noexcept {
    const unsigned span = width < kWordBits ? width : kWordBits;
    unsigned i = 0;
    for (const StatusBit at : BitField{start, static_cast<std::uint8_t>(span)}.each_bit()) {
        word = word.with(at, ((value.v >> i) & 1u) != 0u);
        ++i;
    }
    return word;
}

static_assert(sizeof(StatusWord) == 16);
static_assert(StatusWord::kBits == StatusWord::kBitsPerWord * StatusWord::kWords);
static_assert(StatusWord{}.with(StatusBit{17}, true).has(StatusBit{17}));
static_assert(!StatusWord{}.with(StatusBit{17}, false).has(StatusBit{17}));
static_assert(StatusWord::all().has(StatusBit{127}));

}
