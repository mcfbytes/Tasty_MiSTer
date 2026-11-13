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

class MegaCdLadder final : public BootLadder {
    TASTY_SEAT_RESIDENT(Main);

public:
    MegaCdLadder(const CoreProfile& profile, const LadderContext& ctx)
        : BootLadder(profile, ctx),
          row_(profile.boot_assets) {}

private:
    [[nodiscard]] LadderPc do_pc() const noexcept override {
        TASTY_SEAT_BODY(MegaCdLadder);
        return LadderPc::of(rung_);
    }

    enum class Rung : std::uint8_t {
        Unload = 0,
        Detach = 1,
        PulseAssert = 2,
        PulseRelease = 3,
        Bios = 4,
        MountDisc = 5,
        LayeredBios = 6,
        MountSave = 7,
        Done = LadderPc::kDone,
    };

    [[nodiscard]] Ex<bool> on_step() override;

    RowCursor row_;
    Rung rung_ = Rung::Unload;
};

}
