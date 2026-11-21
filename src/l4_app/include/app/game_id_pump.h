// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "app/game_id.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "infra/xthread/coworker.h"

namespace mister::app {

class GameIdPump final : public xthread::ICoworker {
    TASTY_SEAT_RESIDENT(Ui);

public:
    GameIdPump(const GameIdCell& cell, std::string_view tmp_root);

    void serve() noexcept override;

    void on_pause() noexcept override;

    [[nodiscard]] infra::Tally writes() const noexcept {
        TASTY_SEAT_BODY(GameIdPump);
        return writes_;
    }

    [[nodiscard]] infra::Tally failures() const noexcept {
        TASTY_SEAT_BODY(GameIdPump);
        return failures_;
    }

private:
    const GameIdCell& cell_;
    GameIdCell::Reader reader_{};
    std::string path_;
    infra::Tally writes_{};
    infra::Tally failures_{};
};

}
