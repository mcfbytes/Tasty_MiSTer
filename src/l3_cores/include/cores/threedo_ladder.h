// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "cores/boot_ladder.h"
#include "cores/core_profile.h"
#include "cores/ladder_host.h"
#include "infra/error.h"
#include "os/clock.h"
#include "svc/vfs.h"

namespace mister::cores {

class ThreeDoLadder final : public BootLadder {
    TASTY_SEAT_RESIDENT(Main);

public:
    ThreeDoLadder(const CoreProfile& profile, const LadderContext& ctx)
        : BootLadder(profile, ctx) {}

    [[nodiscard]] std::uint8_t pc() const noexcept override {
        TASTY_SEAT_BODY(ThreeDoLadder);
        return static_cast<std::uint8_t>(rung_);
    }

private:
    enum class Rung : std::uint8_t {
        Unload = 0,
        Detach = 1,
        ResetHold = 2,
        Bios = 3,
        Kanji = 4,
        MountDisc = 5,
        MountSave = 6,
        DiscInfo = 7,
        ResetRelease = 8,
        Done = kDone,
    };

    [[nodiscard]] Ex<bool> on_step() override;

    [[nodiscard]] Ex<bool> rung();

    Rung rung_ = Rung::Unload;
};

}
