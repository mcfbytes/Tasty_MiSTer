// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "cores/boot_ladder.h"
#include "cores/core_profile.h"
#include "cores/ladder_host.h"
#include "cores/row_cursor.h"
#include "infra/error.h"
#include "os/clock.h"
#include "svc/vfs.h"

namespace mister::cores {

class SaturnLadder final : public BootLadder {
    TASTY_SEAT_RESIDENT(Main);

public:
    SaturnLadder(const CoreProfile& profile, const LadderContext& ctx)
        : BootLadder(profile, ctx),
          row_(profile.boot_assets) {}

    [[nodiscard]] bool staged() const noexcept { return stage_; }

private:
    [[nodiscard]] LadderPc do_pc() const noexcept override {
        TASTY_SEAT_BODY(SaturnLadder);
        return LadderPc::of(rung_);
    }
    enum class Rung : std::uint8_t {
        Unload = 0,
        Detach = 1,
        ResetHold = 2,
        Bios = 3,
        MountDisc = 4,
        MountSave = 5,
        BootHeader = 6,
        ResetRelease = 7,
        Done = LadderPc::kDone,
    };

    [[nodiscard]] Ex<bool> on_step() override;

    RowCursor row_;
    Rung rung_ = Rung::Unload;
    bool stage_ = false;
};

}
