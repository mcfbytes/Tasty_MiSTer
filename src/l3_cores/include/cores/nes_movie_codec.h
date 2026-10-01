// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/movie_codec.h"
#include "infra/seat.h"

namespace mister::cores {

class NesMovieCodec : public IMovieCodec {
    TASTY_SEAT_EXEMPT(boot);

public:
    static constexpr std::array<std::uint8_t, 8> kPadBit{0, 1, 2, 3, 7, 6, 5, 4};
    [[nodiscard]] bool starts_log(std::string_view line) const noexcept override;
    [[nodiscard]] Ex<Facts> header_line(std::string_view line,
                                        const Facts& so_far) const noexcept override;
    [[nodiscard]] Ex<void> finish_header(const Facts& f) const noexcept override;
    [[nodiscard]] Ex<Frame> frame(std::string_view line, const Facts& f) const noexcept override;
    [[nodiscard]] SettingNeeds setting_needs(const Facts& f) const noexcept override;
    [[nodiscard]] std::optional<SettingNeed> ram_fill_need(RamFill fill) const noexcept override;
    [[nodiscard]] Raster raster(const Facts& f) const noexcept override;
    [[nodiscard]] PowerOn power_on(const Facts& f) const noexcept override;

    static constexpr std::int32_t kFceuxLead = -3;
    [[nodiscard]] std::int32_t default_lead(const Facts&) const noexcept override {
        return kFceuxLead;
    }

    [[nodiscard]] std::optional<DigestSpan> rom_digest_span(
        std::span<const std::uint8_t> head, std::uint64_t size) const noexcept override;
    [[nodiscard]] std::uint8_t rom_digit() const noexcept override { return 0; }

    [[nodiscard]] static std::optional<std::array<std::uint8_t, 16>> decode_digest(
        std::string_view text) noexcept;
};

consteval bool nes_pad_bits_are_a_permutation() {
    unsigned seen = 0;
    for (const std::uint8_t b : NesMovieCodec::kPadBit) {
        if (b > 7) return false;
        seen |= 1u << b;
    }
    return seen == 0xFFu;
}
static_assert(nes_pad_bits_are_a_permutation(), "the FM2 pad map is a permutation of bits 0-7");

}  // namespace mister::cores
