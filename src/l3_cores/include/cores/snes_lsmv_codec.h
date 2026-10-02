// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/movie_archive.h"
#include "cores/snes_movie_codec.h"
#include "infra/seat.h"

namespace mister::cores {

class SnesLsmvCodec final : public SnesMovieCodec {
    TASTY_SEAT_EXEMPT(boot);

public:
    static constexpr std::array<Button, 12> kPadOrder{
        Button::B,    Button::Y,     Button::Select, Button::Start, Button::Up, Button::Down,
        Button::Left, Button::Right, Button::A,      Button::X,     Button::L,  Button::R};

    static constexpr std::uint8_t kDefaulted = 0x01;
    static constexpr std::uint8_t kHardReset = 0x02;
    static constexpr std::uint8_t kCompact = 0x04;
    static constexpr std::uint8_t kGametype = 0x08;
    static constexpr std::uint8_t kRandomInit = 0x10;

    static constexpr std::int32_t kLsnesLead = 0;

    [[nodiscard]] const MovieArchiveFormat* archive() const noexcept override {
        return &kLsmvArchive;
    }
    [[nodiscard]] bool starts_log(std::string_view line) const noexcept override;
    [[nodiscard]] Ex<Facts> header_line(std::string_view line,
                                        const Facts& so_far) const noexcept override;
    [[nodiscard]] Ex<void> finish_header(const Facts& f) const noexcept override;
    [[nodiscard]] Ex<Frame> frame(std::string_view line, const Facts& f) const noexcept override;
    [[nodiscard]] std::int32_t default_lead(const Facts&) const noexcept override {
        return kLsnesLead;
    }

    [[nodiscard]] std::optional<DigestSpan> rom_digest_span(
        std::span<const std::uint8_t> head, std::uint64_t size) const noexcept override;

private:
    [[nodiscard]] std::optional<WramFill> do_recorder_wram(const Facts& f) const noexcept override;
    [[nodiscard]] RamImageRecipe do_recorder_ram(const Facts& f) const noexcept override;
};

static_assert(snes_pad_order_is_a_permutation(SnesLsmvCodec::kPadOrder),
              "the lsnes gamepad field is a permutation of the core's twelve buttons");

}  // namespace mister::cores
