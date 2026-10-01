// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "cores/movie_codec.h"
#include "infra/seat.h"

namespace mister::svc {
class IChdSource;
}

namespace mister::cores {

class PsxMovieCodec final : public IMovieCodec {
    TASTY_SEAT_EXEMPT(boot);

public:
    struct Button {
        std::string_view octoshock;
        std::string_view nymashock;
        std::uint8_t bit;
    };

    static constexpr std::array<Button, 14> kButtons{{
        {"Right", "Right", 0},
        {"Left", "Left", 1},
        {"Down", "Down", 2},
        {"Up", "Up", 3},
        {"Triangle", "\xE2\x96\xB3", 4},
        {"Circle", "\xE2\x97\x8B", 5},
        {"Cross", "X", 6},
        {"Square", "\xE2\x96\xA1", 7},
        {"Select", "Select", 8},
        {"Start", "Start", 9},
        {"L1", "L1", 10},
        {"R1", "R1", 11},
        {"L2", "L2", 12},
        {"R2", "R2", 13},
    }};

    static constexpr std::uint8_t kColValue = 0x40;
    static constexpr std::uint8_t kColReset = 0x41;
    static constexpr std::uint8_t kColPower = 0x42;
    static constexpr std::uint8_t kColTray = 0x43;

    enum class Emu : std::uint8_t { None, Octoshock, Nymashock };
    struct Layout {
        Emu emu = Emu::None;
        std::uint8_t firmware_region = 0;
        bool platform = false;
        bool log_key = false;
        bool pad_digital = false;
        bool card_off = false;
        [[nodiscard]] static Layout of(std::uint16_t bits) noexcept;
        [[nodiscard]] std::uint16_t bits() const noexcept;
    };

    static constexpr std::int32_t kSessionFormat = 0x20;
    static constexpr std::size_t kTocPrefixBytes = 4 * (3 + 3 * 100);

    static constexpr std::uint64_t kKeySectors = 26;
    static constexpr std::uint64_t kSectorBytes = 2352;

    static constexpr std::uint32_t kChdFrameBytes = 2448;
    static constexpr std::uint32_t kChdHunkMax = 1u << 20;
    static constexpr std::int32_t kLeadMin = -120;
    static constexpr std::int32_t kLeadMax = 1200;
    static_assert(kLeadMin >= INT16_MIN && kLeadMax <= INT16_MAX, "the Arm carries a 16-bit lead");

    [[nodiscard]] const MovieArchiveFormat* archive() const noexcept override;
    [[nodiscard]] bool starts_log(std::string_view line) const noexcept override;
    [[nodiscard]] bool ends_log(std::string_view line) const noexcept override;
    [[nodiscard]] Ex<Facts> header_line(std::string_view line,
                                        const Facts& so_far) const noexcept override;
    [[nodiscard]] Ex<void> finish_header(const Facts& f) const noexcept override;
    [[nodiscard]] Ex<Frame> frame(std::string_view line, const Facts& f) const noexcept override;
    [[nodiscard]] SettingNeeds setting_needs(const Facts& f) const noexcept override;
    [[nodiscard]] Raster raster(const Facts& f) const noexcept override;

    [[nodiscard]] PowerOn power_on(const Facts& f) const noexcept override;
    [[nodiscard]] std::int32_t default_lead(const Facts& f) const noexcept override;
    [[nodiscard]] LeadRange lead_range() const noexcept override { return {kLeadMin, kLeadMax}; }

    [[nodiscard]] std::optional<DigestSpan> rom_digest_span(
        std::span<const std::uint8_t> head, std::uint64_t size) const noexcept override;

    [[nodiscard]] Ex<DigestSource> digest_source(const svc::Vfs& vfs,
                                                 std::string_view rom) const override;
    [[nodiscard]] std::string_view disc_images() const noexcept override {
        return "a Redump .cue/.bin or a chdman .chd of one";
    }

    [[nodiscard]] std::optional<Companion> companion(std::string_view rom,
                                                     const Facts& f) const override;

    [[nodiscard]] std::uint8_t rom_digit() const noexcept override { return 1; }

    [[nodiscard]] static Ex<std::vector<std::uint8_t>> toc_prefix(
        std::string_view cue, std::span<const std::uint64_t> file_sizes);

    [[nodiscard]] static Ex<std::vector<std::uint8_t>> chd_key(svc::IChdSource& chd);

    [[nodiscard]] static Error chd_open_error(const Error& e) noexcept;
};

consteval bool psx_pad_bits_are_a_permutation() {
    unsigned seen = 0;
    for (const PsxMovieCodec::Button& b : PsxMovieCodec::kButtons) {
        if (b.bit > 13) return false;
        seen |= 1u << b.bit;
    }
    return seen == 0x3FFFu;
}
static_assert(psx_pad_bits_are_a_permutation(),
              "the BK2 PSX pad map is a permutation of joy bits 0-13, never a Main-side bit");

}  // namespace mister::cores
