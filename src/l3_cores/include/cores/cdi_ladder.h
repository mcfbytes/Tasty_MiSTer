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

    [[nodiscard]] std::uint8_t pc() const noexcept override {
        TASTY_SEAT_BODY(CdiLadder);
        return static_cast<std::uint8_t>(rung_);
    }

    [[nodiscard]] std::string_view next_last_dir() const noexcept override {
        TASTY_SEAT_BODY(CdiLadder);
        return last_dir_;
    }

private:
    enum class Rung : std::uint8_t {
        MountDisc = 0,
        SaveIndex = 1,
        Save = 2,
        DiscIndex = 3,
        Announce = 4,
        Eject = 10,
        Done = kDone,
    };

    [[nodiscard]] Ex<bool> on_step() override;

    [[nodiscard]] bool steps_at_core_start() const noexcept override {
        TASTY_SEAT_BODY(CdiLadder);
        return true;
    }

    Rung rung_ = Rung::MountDisc;
    std::string last_dir_;
    bool at_core_start_ = false;
};

}
