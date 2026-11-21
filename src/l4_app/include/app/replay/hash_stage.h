// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/replay/replay_stage.h"
#include "app/replay/rom_hasher.h"
#include "infra/seat.h"

namespace mister::app::replay {

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
