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

class NesFm2Codec final : public IMovieFormat {
    TASTY_SEAT_EXEMPT(boot);

public:
    static constexpr std::array<JoyBit, 8> kPadBit{0, 1, 2, 3, 7, 6, 5, 4};
    [[nodiscard]] bool starts_log(std::string_view line) const noexcept override;
    [[nodiscard]] Ex<MovieFacts> header_line(
        std::string_view line,
        const MovieFacts& so_far
    ) const noexcept override;
    [[nodiscard]] Ex<void> finish_header(const MovieFacts& f) const noexcept override;
    [[nodiscard]] Ex<MovieFrame> frame(
        std::string_view line,
        const MovieFacts& f
    ) const noexcept override;

    static constexpr ReplayLead kFceuxLead{-3};

    [[nodiscard]] ReplayLead default_lead(const MovieFacts&) const noexcept override {
        return kFceuxLead;
    }

    [[nodiscard]] std::optional<DigestSpan> rom_digest_span(
        std::span<const std::uint8_t> head,
        proto::FileSize size
    ) const noexcept override;

    [[nodiscard]] static std::optional<DigestValue> decode_digest(std::string_view text) noexcept;
};

consteval bool nes_pad_bits_are_a_permutation() {
    proto::JoyMask seen{};
    for (const JoyBit b : NesFm2Codec::kPadBit)
        seen = seen.with(b.mask());
    return seen == proto::JoyMask{0xFFu};
}

static_assert(nes_pad_bits_are_a_permutation(), "the FM2 pad map is a permutation of bits 0-7");

}
