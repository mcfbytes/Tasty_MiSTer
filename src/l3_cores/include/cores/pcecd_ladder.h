// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "cores/boot_asset.h"
#include "cores/boot_ladder.h"
#include "cores/core_profile.h"
#include "cores/ladder_host.h"
#include "infra/error.h"
#include "os/clock.h"
#include "svc/vfs.h"

namespace mister::cores {

class PceCdLadder final : public BootLadder {
    TASTY_SEAT_RESIDENT(Main);

public:
    PceCdLadder(const CoreProfile& profile, const LadderContext& ctx) : BootLadder(profile, ctx) {}

    [[nodiscard]] std::uint8_t pc() const noexcept override {
        TASTY_SEAT_BODY(PceCdLadder);
        return static_cast<std::uint8_t>(rung_);
    }

private:
    enum class Rung : std::uint8_t {
        MountDisc = 0,
        SgxProbe = 1,
        Bios = 2,
        NotifyMount = 3,
        Eject = 10,
        PulseRelease = 11,
        Done = kDone,
    };

    [[nodiscard]] Ex<bool> on_step() override;

    [[nodiscard]] Ex<AssetWait> load_pce_bios(const BootAsset& row);
    [[nodiscard]] Ex<AssetWait> finish_pce_bios_();

    Rung rung_ = Rung::MountDisc;
    std::uint8_t pce_region_ = 0;
};

}
