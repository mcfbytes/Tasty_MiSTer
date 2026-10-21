// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/replay_stage.h"
#include "app/rom_hasher.h"
#include "infra/seat.h"

namespace mister::app {

class HashStage final : public IReplayStage {
    TASTY_SEAT_RESIDENT(Ui);

public:
    [[nodiscard]] Stage level() const noexcept override;
    [[nodiscard]] Step enter(Ctx ctx) noexcept override;
    [[nodiscard]] Step tick(Ctx ctx) noexcept override;
    [[nodiscard]] Step stop(Ctx ctx) noexcept override;

private:
    [[nodiscard]] Step finish_rom_(Ctx ctx) noexcept;

    RomHasher hasher_{};
    bool firmware_pass_ = false;
};

}
