// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>

#include "hal/types.h"
#include "hal/spi_halves.h"
#include "proto/bit_field.h"
#include "proto/status_bit.h"
#include "proto/types.h"
#include "infra/seat.h"

namespace mister::proto {

struct StatusWord {
    static constexpr unsigned kWords = 8;
    std::array<hal::SpiWord, kWords> words{};

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
        out.words[w] = value ? out.words[w].with(bit.lane_mask())
                             : out.words[w].without(bit.lane_mask());
        return out;
    }

    [[nodiscard]] static constexpr StatusWord of(hal::SpiHalves halves) noexcept {
        StatusWord w{};
        w.words[0] = halves.low;
        w.words[1] = halves.high;
        return w;
    }

    static constexpr unsigned kRegisters = kWords / 2;
    static_assert(kRegisters * 2 == kWords);

    [[nodiscard]] static constexpr StatusWord of_registers(
        std::span<const hal::SpiHalves, kRegisters> regs
    ) noexcept {
        StatusWord w{};
        for (unsigned r = 0; r < kRegisters; ++r) {
            w.words[2 * r] = regs[r].low;
            w.words[2 * r + 1] = regs[r].high;
        }
        return w;
    }

    [[nodiscard]] static constexpr StatusWord all() noexcept {
        StatusWord w{};
        for (auto& word : w.words)
            word = hal::SpiWord::all_ones();
        return w;
    }

    using Bytes = std::array<std::uint8_t, kWords * sizeof(hal::SpiWord)>;

    [[nodiscard]] static constexpr StatusWord from_bytes(
        std::span<const std::uint8_t, sizeof(Bytes)> b
    ) noexcept {
        StatusWord w{};
        for (unsigned i = 0; i < kWords; ++i)
            w.words[i] = hal::SpiWord::load_le(
                b.subspan(i * sizeof(hal::SpiWord)).first<sizeof(hal::SpiWord)>()
            );
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
        for (unsigned i = 0; i < kWords; ++i) {
            words[i].store_le(
                std::span<std::uint8_t>(out)
                    .subspan(i * sizeof(hal::SpiWord))
                    .first<sizeof(hal::SpiWord)>()
            );
        }
        return out;
    }

    friend constexpr bool operator==(const StatusWord&, const StatusWord&) = default;

private:
    [[nodiscard]] constexpr bool bit_at(StatusBit bit) const noexcept {
        return words[bit.word()].intersects(bit.lane_mask());
    }
};

constexpr FieldValue BitField::read(const StatusWord& word) const noexcept {
    return word.field(*this);
}

constexpr StatusWord BitField::write(StatusWord word, FieldValue value) const noexcept {
    const unsigned span = width < kWordBits ? width : kWordBits;
    for (unsigned i = 0; i < span; ++i) {
        if (const auto at = start.plus(i))
            word = word.with(*at, value.has_bit(i));
    }
    return word;
}

static_assert(sizeof(StatusWord) == 16);
static_assert(std::is_same_v<decltype(StatusWord::words)::value_type, hal::SpiWord>);
static_assert(StatusBit::kCount == StatusWord::kWords * std::numeric_limits<std::uint16_t>::digits);
static_assert(StatusWord{}.with(StatusBit{17}, true).has(StatusBit{17}));
static_assert(!StatusWord{}.with(StatusBit{17}, false).has(StatusBit{17}));
static_assert(StatusWord::all().has(StatusBit{127}));

}
