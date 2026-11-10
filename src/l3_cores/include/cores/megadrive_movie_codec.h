// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "cores/i_replay_core.h"
#include "cores/movie_facts.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores {

class MegaDriveMovieCodec final : public IReplayCore {
    TASTY_SEAT_EXEMPT(boot);

public:
    enum class Pad : std::uint8_t { None, Three, Six };
    enum class Sys : std::uint8_t { None, First, Last };

    struct Layout {

        enum class GpgxRegion : std::uint8_t { Auto, Usa, Europe, JapanNtsc };

        enum class CoreRegion : std::uint8_t { Japan, Usa, Europe };
        Pad p1 = Pad::None;
        Pad p2 = Pad::None;
        Sys sys = Sys::None;
        bool reset_first = false;
        bool gmv = false;
        GpgxRegion region = GpgxRegion::Auto;
        bool pal_key = false;
        bool platform = false;
        bool core = false;
        [[nodiscard]] static Layout of(std::uint16_t bits) noexcept;
        [[nodiscard]] std::uint16_t bits() const noexcept;

        [[nodiscard]] std::optional<CoreRegion> core_region() const noexcept;
    };

    [[nodiscard]] SettingNeeds setting_needs(const MovieFacts& f) const noexcept override;
    [[nodiscard]] Raster raster(const MovieFacts& f) const noexcept override;
    [[nodiscard]] PowerOn power_on(const MovieFacts& f) const noexcept override;

    [[nodiscard]] proto::FileSlotDigit rom_slot_digit() const noexcept override {
        return *proto::FileSlotDigit::from(1);
    }

    [[nodiscard]] std::string_view games_folder() const noexcept override { return "Genesis"; }
};

}
