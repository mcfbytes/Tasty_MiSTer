// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/movie_codec.h"

namespace mister::cores {

class SnesMovieCodec : public IMovieCodec {
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

    static constexpr std::uint8_t kSaveStateBit = 12;

    enum class WramFill : std::uint8_t { Pattern9966, Pattern00FF, Flat55, FlatFF };

    static constexpr std::uint64_t kRomMin = 128 * 1024;

    [[nodiscard]] static constexpr std::uint32_t bit(Button b) noexcept {
        return 1u << static_cast<std::uint8_t>(b);
    }

    [[nodiscard]] SettingNeeds setting_needs(const Facts& f) const noexcept final;
    [[nodiscard]] Raster raster(const Facts& f) const noexcept final;
    [[nodiscard]] PowerOn power_on(const Facts& f) const noexcept final;
    [[nodiscard]] std::uint8_t rom_digit() const noexcept final { return 1; }

    [[nodiscard]] RamImageRecipe power_on_ram(const Facts& f) const noexcept final {
        return do_recorder_ram(f);
    }
    static constexpr std::uint32_t kWramBytes = 128 * 1024;
    static constexpr std::uint32_t kAramBytes = 64 * 1024;

protected:
    SnesMovieCodec() = default;

    [[nodiscard]] static std::optional<DigestSpan> past_header(std::uint64_t size,
                                                               std::uint64_t header) noexcept;

    struct KeyValue {
        std::string_view key;
        std::string_view value;
    };
    [[nodiscard]] static KeyValue split(std::string_view line) noexcept;
    [[nodiscard]] static std::string_view trim(std::string_view s) noexcept;
    [[nodiscard]] static Error refuse(Refusal r, std::uint16_t site) noexcept {
        return Error{Errc::bad_format, site, static_cast<std::uint32_t>(r)};
    }

private:
    [[nodiscard]] virtual std::optional<WramFill> do_recorder_wram(
        const Facts& f) const noexcept = 0;

    [[nodiscard]] virtual RamImageRecipe do_recorder_ram(const Facts& f) const noexcept = 0;
};

template <std::size_t N>
consteval bool snes_pad_order_is_a_permutation(const std::array<SnesMovieCodec::Button, N>& order) {
    unsigned seen = 0;
    for (const SnesMovieCodec::Button b : order) {
        const auto v = static_cast<unsigned>(b);
        if (v >= static_cast<unsigned>(SnesMovieCodec::Button::kCount) ||
            v == SnesMovieCodec::kSaveStateBit || (seen & (1u << v)) != 0)
            return false;
        seen |= 1u << v;
    }
    return N == static_cast<std::size_t>(SnesMovieCodec::Button::kCount) && seen == 0xFFFu;
}

}  // namespace mister::cores
