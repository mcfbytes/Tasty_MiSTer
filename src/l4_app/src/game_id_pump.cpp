// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/game_id_pump.h"

#include "app/process_integration.h"

namespace mister::app {

GameIdPump::GameIdPump(const GameIdCell& cell, std::string_view tmp_root)
    : cell_(cell),
      path_(std::string{tmp_root} + "/GAMEID") {}

void GameIdPump::serve() noexcept {
    TASTY_SEAT_BODY(GameIdPump);
    const auto id = reader_.take_if_changed(cell_);
    if (!id || !id->writes())
        return;
    if (proc::write_tmp_handoff(path_.c_str(), id->body().view()))
        writes_.add();
    else
        failures_.add();
}

void GameIdPump::on_pause() noexcept {
    TASTY_SEAT_BODY(GameIdPump);
    (void)reader_.take_if_changed(cell_);
}

}
