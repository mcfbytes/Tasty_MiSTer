// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "cores/i_replay_core.h"
#include "cores/movie_facts.h"
#include "cores/types.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores {

class PsxMovieCodec final : public IReplayCore {
    TASTY_SEAT_EXEMPT(boot);

public:
    struct Button {
        std::string_view octoshock;
        std::string_view nymashock;
        JoyBit bit;
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

    static constexpr std::uint16_t kPadBitCount = 14;
    static constexpr proto::JoyMask kPadBitMask{(1u << kPadBitCount) - 1u};
    static constexpr ReplayLead kLeadMin{-120};
    static constexpr ReplayLead kLeadMax{1200};
    static_assert(
        kLeadMin.fits_in<std::int16_t>() && kLeadMax.fits_in<std::int16_t>(),
        "the Arm carries a 16-bit lead"
    );

    enum class Emu : std::uint8_t { None, Octoshock, Nymashock };

    enum class FirmwareRegion : std::uint8_t { None, Usa, Japan, Europe };

    struct Layout {
        Emu emu = Emu::None;
        FirmwareRegion firmware_region = FirmwareRegion::None;
        bool platform = false;
        bool log_key = false;
        bool pad_digital = false;
        bool card_off = false;
        [[nodiscard]] static Layout of(std::uint16_t bits) noexcept;
        [[nodiscard]] std::uint16_t bits() const noexcept;
    };

    [[nodiscard]] SettingNeeds setting_needs(const MovieFacts& f) const noexcept override;
    [[nodiscard]] Raster raster(const MovieFacts& f) const noexcept override;

    [[nodiscard]] PowerOn power_on(const MovieFacts& f) const noexcept override;

    [[nodiscard]] LeadRange lead_range() const noexcept override { return {kLeadMin, kLeadMax}; }

    [[nodiscard]] std::string_view disc_images() const noexcept override {
        return "a Redump .cue/.bin or a chdman .chd of one";
    }

    [[nodiscard]] std::optional<Companion> companion(
        std::string_view rom,
        const MovieFacts& f
    ) const override;

    [[nodiscard]] proto::FileSlotDigit rom_slot_digit() const noexcept override {
        return *proto::FileSlotDigit::from(1);
    }

    [[nodiscard]] bool rom_is_disc() const noexcept override { return true; }
};

consteval bool psx_pad_bits_are_a_permutation() {
    proto::JoyMask seen{};
    for (const PsxMovieCodec::Button& b : PsxMovieCodec::kButtons)
        seen = seen.with(b.bit.mask());
    return seen == PsxMovieCodec::kPadBitMask;
}

static_assert(
    psx_pad_bits_are_a_permutation(),
    "the BK2 PSX pad map is a permutation of joy bits 0-13, never a Main-side bit"
);

}
