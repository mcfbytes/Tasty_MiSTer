// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/movie_archive.h"
#include "cores/movie_codec.h"
#include "cores/snes_movie_codec.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::cores {

class SnesLsmvCodec final : public IMovieFormat {
    TASTY_SEAT_EXEMPT(boot);

public:
    using Button = SnesMovieCodec::Button;

    static constexpr std::array<Button, 12> kPadOrder{
        Button::B,
        Button::Y,
        Button::Select,
        Button::Start,
        Button::Up,
        Button::Down,
        Button::Left,
        Button::Right,
        Button::A,
        Button::X,
        Button::L,
        Button::R
    };

    static constexpr std::uint8_t kDefaulted = 0x01;
    static constexpr std::uint8_t kHardReset = 0x02;
    static constexpr std::uint8_t kCompact = 0x04;
    static constexpr std::uint8_t kGametype = 0x08;
    static constexpr std::uint8_t kRandomInit = 0x10;

    static constexpr ReplayLead kLsnesLead{0};

    [[nodiscard]] infra::OptRef<const MovieArchiveFormat> archive() const noexcept override {
        return infra::OptRef<const MovieArchiveFormat>{kLsmvArchive};
    }

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

    [[nodiscard]] ReplayLead default_lead(const MovieFacts&) const noexcept override {
        return kLsnesLead;
    }

    [[nodiscard]] std::optional<DigestSpan> rom_digest_span(
        std::span<const std::uint8_t> head,
        proto::FileSize size
    ) const noexcept override;
};

static_assert(
    snes_pad_order_is_a_permutation(SnesLsmvCodec::kPadOrder),
    "the lsnes gamepad field is a permutation of the core's twelve buttons"
);

}
