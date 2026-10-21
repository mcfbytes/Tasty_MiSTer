// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "app/conf_str_cell.h"
#include "app/replay_stage.h"
#include "app/replay_status.h"
#include "infra/seat.h"

namespace mister::app {

class RunStage final : public IReplayStage {
    TASTY_SEAT_RESIDENT(Ui);

public:
    [[nodiscard]] Stage level() const noexcept override;
    [[nodiscard]] Step enter(Ctx ctx) noexcept override;
    [[nodiscard]] Step tick(Ctx ctx) noexcept override;
    [[nodiscard]] Step stop(Ctx ctx) noexcept override;

private:
    [[nodiscard]] Step arm_(Ctx ctx) noexcept;
    [[nodiscard]] Step tick_arming_(Ctx ctx, const ReplayStatus& s, bool fresh) noexcept;
    void stream_(Ctx ctx) noexcept;
    void checkpoints_(Ctx ctx, const ReplayStatus& s) noexcept;
    [[nodiscard]] std::optional<ReplayStatus> sample_status_(Ctx ctx) const noexcept;
    void publish_(Ctx ctx, ReplayOp op) noexcept;
    [[nodiscard]] Step finish_(Ctx ctx, const ReplayStatus& s) noexcept;

    Stage level_ = Stage::Arming;
    std::int64_t since_ns_ = 0;
    std::size_t next_run_ = 0;
    bool end_pushed_ = false;
    std::optional<std::int64_t> disc_ns_{};
    ConfStrText conf_{};
};

}
