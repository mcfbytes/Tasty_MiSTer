// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/conf_str_cell.h"
#include "app/replay_stage.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/status_word.h"

namespace mister::proto {
class ItemTable;
}

namespace mister::app {

class SettleStage final : public IReplayStage {
    TASTY_SEAT_RESIDENT(Ui);

public:
    [[nodiscard]] Stage level() const noexcept override;
    [[nodiscard]] Step enter(Ctx ctx) noexcept override;
    [[nodiscard]] Step tick(Ctx ctx) noexcept override;
    [[nodiscard]] Step stop(Ctx ctx) noexcept override;

private:
    [[nodiscard]] Step settle_(Ctx ctx) noexcept;

    [[nodiscard]] bool set_unmet_(
        Ctx ctx,
        const proto::StatusWord& live,
        const cores::IMovieCodec::SettingNeeds& needs,
        infra::OptRef<const proto::ItemTable> labels
    ) noexcept;
    [[nodiscard]] std::optional<std::uint8_t> rom_slot_(Ctx ctx) noexcept;
    static void load_checkpoints_(Ctx ctx) noexcept;

    bool waiting_ = false;
    std::int64_t since_ns_ = 0;
    ConfStrText conf_{};
};

}
