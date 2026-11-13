// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/bk2_column.h"
#include "cores/movie_archive.h"
#include "cores/movie_codec.h"
#include "cores/types.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::cores {

class NesBk2Codec final : public IMovieFormat {
    TASTY_SEAT_EXEMPT(boot);

public:
    static constexpr std::string_view kPlatformName = "NES";

    static constexpr ReplayLead kNesHawkLead{-2};

    [[nodiscard]] infra::OptRef<const MovieArchiveFormat> archive() const noexcept override {
        return infra::OptRef<const MovieArchiveFormat>{kBk2Archive};
    }

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

    [[nodiscard]] ReplayLead default_lead(const MovieFacts&) const noexcept override {
        return kNesHawkLead;
    }

    [[nodiscard]] std::optional<DigestSpan> rom_digest_span(
        std::span<const std::uint8_t> head,
        proto::FileSize size
    ) const noexcept override;

    [[nodiscard]] Ex<DigestSource> digest_source(
        const svc::Vfs& vfs,
        std::string_view rom
    ) const override;

    struct PadName {
        std::string_view name;
        JoyBit bit;
    };

    static constexpr std::array<PadName, 8> kPadNames{
        {{"Up", 3},
         {"Down", 2},
         {"Left", 1},
         {"Right", 0},
         {"Start", 7},
         {"Select", 6},
         {"B", 5},
         {"A", 4}}
    };

    static constexpr bool kReservesGroupEnd = true;

    [[nodiscard]] static Ex<Bk2Column> column_of(std::string_view name) noexcept;
};

consteval bool nes_bk2_pad_bits_are_a_permutation() {
    unsigned seen = 0;
    for (const NesBk2Codec::PadName& p : NesBk2Codec::kPadNames) {
        if (p.bit.v > 7)
            return false;
        if ((seen & (1u << p.bit.v)) != 0)
            return false;
        seen |= 1u << p.bit.v;
    }
    return seen == 0xFFu;
}

static_assert(
    nes_bk2_pad_bits_are_a_permutation(),
    "BizHawk's NES pad names are a permutation of bits 0-7"
);

}
