// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cores/boot_ladder.h"
#include "cores/core_profile.h"
#include "cores/ladder_context.h"
#include "infra/error.h"

namespace mister::cores {

class CdiLadder final : public BootLadder {
    TASTY_SEAT_RESIDENT(Main);

public:
    CdiLadder(const CoreProfile& profile, const LadderContext& ctx);

private:
    [[nodiscard]] LadderPc do_pc() const noexcept override {
        TASTY_SEAT_BODY(CdiLadder);
        return LadderPc::of(rung_);
    }

    [[nodiscard]] LadderCarry do_carry() const noexcept override {
        TASTY_SEAT_BODY(CdiLadder);
        return LadderCarry{.last_dir = last_dir_};
    }
    enum class Rung : std::uint8_t {
        MountDisc = 0,
        SaveIndex = 1,
        Save = 2,
        DiscIndex = 3,
        Announce = 4,
        Eject = 10,
        Done = LadderPc::kDone,
    };

    [[nodiscard]] Ex<bool> on_step() override;

    [[nodiscard]] bool steps_at_core_start() const noexcept override {
        TASTY_SEAT_BODY(CdiLadder);
        return true;
    }

    Rung rung_ = Rung::MountDisc;
    std::string last_dir_;
};

}
