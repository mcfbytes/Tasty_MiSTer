// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/movie_codec.h"
#include "cores/types.h"
#include "infra/seat.h"

namespace mister::cores {

class SnesMovieCodec final : public IReplayCore {
    TASTY_SEAT_EXEMPT(boot);

public:
    enum class Button : std::uint8_t {
        Right,
        Left,
        Down,
        Up,
        A,
        B,
        X,
        Y,
        L,
        R,
        Select,
        Start,
        kCount
    };

    static constexpr JoyBit kSaveStateBit{12};

    enum class WramFill : std::uint8_t { Pattern9966, Pattern00FF, Flat55, FlatFF };

    [[nodiscard]] static constexpr proto::FieldValue initial_wram_value(WramFill fill) noexcept {
        return proto::FieldValue{static_cast<std::uint32_t>(fill)};
    }

    static constexpr std::uint32_t kWramBytes = 128 * 1024;
    static constexpr std::uint32_t kAramBytes = 64 * 1024;

    static constexpr std::uint64_t kRomMin = 128 * 1024;

    [[nodiscard]] static constexpr proto::JoyMask bit(Button b) noexcept {
        return JoyBit{static_cast<std::uint8_t>(b)}.mask();
    }

    [[nodiscard]] SettingNeeds setting_needs(const MovieFacts& f) const noexcept override;
    [[nodiscard]] Raster raster(const MovieFacts& f) const noexcept override;
    [[nodiscard]] PowerOn power_on(const MovieFacts& f) const noexcept override;

    [[nodiscard]] proto::FileSlotDigit rom_slot_digit() const noexcept override {
        return *proto::FileSlotDigit::from(1);
    }

    [[nodiscard]] static std::optional<IMovieFormat::DigestSpan> past_header(
        proto::FileSize size,
        proto::FileSize header
    ) noexcept;
};

template <std::size_t N>
consteval bool snes_pad_order_is_a_permutation(const std::array<SnesMovieCodec::Button, N>& order) {
    proto::JoyMask seen{};
    for (const SnesMovieCodec::Button b : order) {
        const proto::JoyMask bits = SnesMovieCodec::bit(b);
        if (bits == SnesMovieCodec::kSaveStateBit.mask() || seen.any(bits))
            return false;
        seen = seen.with(bits);
    }
    return N == static_cast<std::size_t>(SnesMovieCodec::Button::kCount)
        && seen == proto::JoyMask{0xFFFu};
}

}
