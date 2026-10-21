// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/boot_ladder.h"
#include "cores/core_profile.h"
#include "cores/ladder_context.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::cores {

class NeoGeoCdLadder final : public BootLadder {
    TASTY_SEAT_RESIDENT(Main);

public:
    NeoGeoCdLadder(const CoreProfile& profile, const LadderContext& ctx);

    [[nodiscard]] std::uint8_t pc() const noexcept override {
        TASTY_SEAT_BODY(NeoGeoCdLadder);
        return static_cast<std::uint8_t>(rung_);
    }

private:
    enum class Rung : std::uint8_t {
        Unload = 0,
        Walk = 1,
        MountDisc = 2,
        Reset = 3,
        Release = 4,
        Done = kDone,
    };

    [[nodiscard]] Ex<bool> on_step() override;
    [[nodiscard]] bool walk_answered_();

    Rung rung_ = Rung::Unload;
    std::uint32_t walk_gen_ = 0;
    bool walk_ordered_ = false;
    std::int64_t refused_since_ = -1;
};

}
