// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/i_movie_format.h"
#include "cores/megadrive_movie_codec.h"
#include "cores/movie_facts.h"
#include "cores/movie_frame.h"
#include "cores/types.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores {

class MegaDriveBk2Format : public IMovieFormat {
    TASTY_SEAT_EXEMPT(boot);

public:
    using Pad = MegaDriveMovieCodec::Pad;
    using Sys = MegaDriveMovieCodec::Sys;
    using Layout = MegaDriveMovieCodec::Layout;

    static constexpr std::array<JoyBit, 12> kPadBit{3, 2, 1, 0, 4, 5, 6, 7, 9, 10, 11, 8};
    static constexpr std::string_view kPadMnemonic = "UDLRABCSXYZM";
    static constexpr std::array<std::string_view, 12>
        kPadName{"Up", "Down", "Left", "Right", "A", "B", "C", "Start", "X", "Y", "Z", "Mode"};

    static constexpr std::string_view kPlatformName = "GEN";

    static constexpr ReplayLead kGpgxLead{-3};

    [[nodiscard]] infra::OptRef<const MovieArchiveFormat> archive() const noexcept override;
    [[nodiscard]] bool starts_log(std::string_view line) const noexcept override;
    [[nodiscard]] bool ends_log(std::string_view line) const noexcept override;
    [[nodiscard]] Ex<MovieFacts> header_line(
        std::string_view line,
        const MovieFacts& so_far
    ) const noexcept override;
    [[nodiscard]] Ex<void> finish_header(const MovieFacts& f) const noexcept override;
    [[nodiscard]] Ex<MovieFrame> frame(
        std::string_view line,
        const MovieFacts& f
    ) const noexcept override;

    [[nodiscard]] ReplayLead default_lead(const MovieFacts& f) const noexcept override;

    [[nodiscard]] std::optional<DigestSpan> rom_digest_span(
        std::span<const std::uint8_t> head,
        proto::FileSize size
    ) const noexcept override;
};

consteval bool megadrive_pad_bits_are_a_permutation() {
    proto::JoyMask seen{};
    for (const JoyBit b : MegaDriveBk2Format::kPadBit)
        seen = seen.with(b.mask());
    return seen == proto::JoyMask{0xFFFu};
}

static_assert(
    megadrive_pad_bits_are_a_permutation(),
    "the BK2 Genesis pad map is a permutation of JoyMask bits 0-11"
);
static_assert(MegaDriveBk2Format::kPadMnemonic.size() == MegaDriveBk2Format::kPadBit.size());

}
