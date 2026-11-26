// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "cores/types.h"
#include "hal/reg_field.h"
#include "hal/link_halves.h"
#include "infra/affine.h"

namespace mister::cores::atari8 {

using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator/;

enum class TapeLevel : std::uint8_t { Low, High };

[[nodiscard]] constexpr TapeLevel flipped(TapeLevel level) noexcept {
    return level == TapeLevel::Low ? TapeLevel::High : TapeLevel::Low;
}

enum class BitOrder : std::uint8_t { LsbFirst, MsbFirst };

[[nodiscard]] constexpr std::array<TapeLevel, 8> bits_of(std::byte b, BitOrder order) noexcept {
    std::array<TapeLevel, 8> levels{};
    for (std::size_t i = 0; i < levels.size(); ++i) {
        const std::size_t bit = order == BitOrder::MsbFirst ? levels.size() - 1 - i : i;
        levels[i] = (std::to_integer<unsigned>(b) & (1u << bit)) != 0 ? TapeLevel::High
                                                                      : TapeLevel::Low;
    }
    return levels;
}

enum class TapeMode : std::uint8_t { Standard, Turbo };

struct TapeRate {
    std::uint32_t v = 0;
    friend constexpr bool operator==(TapeRate, TapeRate) = default;
};

struct TapeTicks {
    std::uint32_t v = 0;
    using affine_count = TapeTicks;

    static const TapeTicks kTail;

    [[nodiscard]] static constexpr TapeTicks period_of(ClockHz core, TapeRate rate) noexcept {
        constexpr std::uint32_t kRounding = 2;
        return TapeTicks{(core.v + rate.v / kRounding) / rate.v};
    }

    [[nodiscard]] constexpr TapeTicks halved() const noexcept {
        constexpr std::uint32_t kHalves = 2;
        return TapeTicks{v / kHalves};
    }

    [[nodiscard]] constexpr hal::LinkHalves entry(TapeLevel level) const noexcept {
        return hal::LinkHalves::of(std::to_underlying(level) | (v << 1));
    }

    friend constexpr bool operator==(TapeTicks, TapeTicks) = default;
};

inline constexpr TapeTicks TapeTicks::kTail{16};

using TapeSilence = std::chrono::duration<std::uint32_t, std::milli>;

inline constexpr TapeSilence kMaxSilence{60000};

struct PwmSettings {
    TapeLevel mark;
    BitOrder order;

    [[nodiscard]] static constexpr std::optional<PwmSettings> of(std::byte aux0) noexcept {
        constexpr hal::RegField<std::uint8_t> kOrderField{2, 0x1};
        constexpr hal::RegField<std::uint8_t> kMarkField{0, 0x3};
        constexpr std::uint8_t kLow = 0b01;
        constexpr std::uint8_t kHigh = 0b10;
        const std::uint8_t word = std::to_integer<std::uint8_t>(aux0);
        const BitOrder order = kOrderField.test(word) ? BitOrder::MsbFirst : BitOrder::LsbFirst;
        const std::uint8_t mark = kMarkField.get(word);
        if (mark == kLow)
            return PwmSettings{TapeLevel::Low, order};
        if (mark == kHigh)
            return PwmSettings{TapeLevel::High, order};
        return std::nullopt;
    }

    friend constexpr bool operator==(PwmSettings, PwmSettings) = default;
};

}
