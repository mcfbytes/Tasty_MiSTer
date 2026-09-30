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

class SnesBk2Codec final : public SnesMovieCodec {
    TASTY_SEAT_EXEMPT(boot);

public:
    static constexpr std::uint8_t kColReset = 1;
    static constexpr std::uint8_t kColPower = 2;
    static constexpr std::uint8_t kColSubframe = 3;
    static constexpr std::uint8_t kColResetDelay = 4;
    static constexpr std::uint8_t kColPad = 0x80;
    static constexpr std::uint8_t kColGroupEnd = 0xFF;

    static constexpr std::uint8_t kMovieVersion = 0x01;
    static constexpr std::uint8_t kPlatform = 0x02;
    static constexpr std::uint8_t kLogKey = 0x04;
    static constexpr std::uint8_t kSha1 = 0x08;

    static constexpr std::int32_t kBizHawkLead = -1;

    [[nodiscard]] const MovieArchiveFormat* archive() const noexcept override {
        return &kBk2Archive;
    }
    [[nodiscard]] bool starts_log(std::string_view line) const noexcept override;
    [[nodiscard]] bool ends_log(std::string_view line) const noexcept override;
    [[nodiscard]] Ex<Facts> header_line(std::string_view line,
                                        const Facts& so_far) const noexcept override;
    [[nodiscard]] Ex<void> finish_header(const Facts& f) const noexcept override;
    [[nodiscard]] Ex<Frame> frame(std::string_view line, const Facts& f) const noexcept override;
    [[nodiscard]] std::int32_t default_lead(const Facts&) const noexcept override {
        return kBizHawkLead;
    }

    [[nodiscard]] std::optional<DigestSpan> rom_digest_span(
        std::span<const std::uint8_t> head, std::uint64_t size) const noexcept override;

    struct PadName {
        std::string_view name;
        Button button;
    };
    static constexpr std::array<PadName, 12> kPadNames{{{"Up", Button::Up},
                                                        {"Down", Button::Down},
                                                        {"Left", Button::Left},
                                                        {"Right", Button::Right},
                                                        {"Select", Button::Select},
                                                        {"Start", Button::Start},
                                                        {"Y", Button::Y},
                                                        {"B", Button::B},
                                                        {"X", Button::X},
                                                        {"A", Button::A},
                                                        {"L", Button::L},
                                                        {"R", Button::R}}};

    [[nodiscard]] static Ex<std::uint8_t> column_of(std::string_view name) noexcept;
};

consteval std::array<SnesMovieCodec::Button, 12> bk2_pad_order() {
    std::array<SnesMovieCodec::Button, 12> out{};
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = SnesBk2Codec::kPadNames[i].button;
    return out;
}
static_assert(snes_pad_order_is_a_permutation(bk2_pad_order()),
              "BizHawk's SNES pad names are a permutation of the core's twelve buttons");

}  // namespace mister::cores
