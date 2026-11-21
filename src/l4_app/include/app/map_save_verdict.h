// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/input/joymap_ends.h"
#include "app/recents/write_verdict.h"
#include "infra/seat.h"

namespace mister::app {

class MapSaveVerdict final : public app::recents::IWriteVerdict {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit MapSaveVerdict(const input::JoyMapEnds& ends) noexcept : ends_(ends) {}

    void on_written(bool durable) noexcept override {
        TASTY_SEAT_BODY(MapSaveVerdict);
        if (!durable)
            return;
        ends_.hotplug.request();
        ends_.input_ctrl.kick();
    }

private:
    const input::JoyMapEnds& ends_;
};

}
