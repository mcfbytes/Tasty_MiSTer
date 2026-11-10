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

class NesMovieCodec final : public IReplayCore {
    TASTY_SEAT_EXEMPT(boot);

public:
    [[nodiscard]] SettingNeeds setting_needs(const MovieFacts& f) const noexcept override;
    [[nodiscard]] std::optional<SettingNeed> ram_fill_need(RamFill fill) const noexcept override;
    [[nodiscard]] Raster raster(const MovieFacts& f) const noexcept override;
    [[nodiscard]] PowerOn power_on(const MovieFacts& f) const noexcept override;

    [[nodiscard]] proto::FileSlotDigit rom_slot_digit() const noexcept override {
        return *proto::FileSlotDigit::from(0);
    }
};

}
