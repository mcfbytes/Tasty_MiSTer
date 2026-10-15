// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/board_ops.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::hal {
class IBridgeSequencer;
}

namespace mister::fw {

class BoardReset final : public app::IBoardReset {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr unsigned kResetSettleMs = 5000;

    explicit BoardReset(hal::IBridgeSequencer& bridges,
                        infra::OptRef<app::ISleeper> sleeper = {}) noexcept
        : bridges_(bridges), sleeper_(sleeper) {}
    BoardReset(const BoardReset&) = delete;
    BoardReset& operator=(const BoardReset&) = delete;

    void board_reset() override;

private:
    hal::IBridgeSequencer& bridges_;
    infra::OptRef<app::ISleeper> sleeper_{};
};

}  // namespace mister::fw
