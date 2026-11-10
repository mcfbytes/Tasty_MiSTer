// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "cores/i_rom_shape.h"
#include "cores/types.h"
#include "infra/seat.h"

namespace mister::cores::neogeo {

struct WordCount {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr WordCount of(std::span<const std::uint8_t> piece) noexcept {
        return WordCount{static_cast<std::uint32_t>(piece.size() / 2u)};
    }

    [[nodiscard]] constexpr WordCount rounded_up_to(WordCount block) const noexcept {
        return WordCount{((v + block.v - 1u) / block.v) * block.v};
    }

    [[nodiscard]] constexpr bool holds(std::uint32_t i) const noexcept { return i < v; }

    template <class T>
    [[nodiscard]] std::vector<T> sized_vector() const {
        return std::vector<T>(v);
    }

    [[nodiscard]] constexpr WordCount halfword_extent() const noexcept {
        return WordCount{v == 0u ? 0u : (v << 1) - 1u};
    }

    template <class T>
    [[nodiscard]] constexpr std::span<T> head_of(std::span<T> buf) const noexcept {
        return buf.first(v);
    }

    template <class T>
    [[nodiscard]] constexpr std::span<T> first_of(std::span<T> buf) const noexcept {
        return buf.first(v < buf.size() ? v : buf.size());
    }

    friend constexpr bool operator==(WordCount, WordCount) = default;
};

class SprShape final : public IRomShape {
    TASTY_SEAT_RESIDENT(Main);

public:
    void transform(std::span<std::uint8_t> piece) noexcept override;

    void transform_skip(
        std::span<const std::uint16_t> words,
        std::span<std::uint16_t> out,
        WordCount count
    ) noexcept;

private:
    std::vector<std::uint16_t> src_;
    std::vector<std::uint16_t> out_;
};

}
