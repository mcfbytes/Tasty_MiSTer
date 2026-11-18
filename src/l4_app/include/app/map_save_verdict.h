// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/joymap_ends.h"
#include "app/write_verdict.h"
#include "infra/seat.h"

namespace mister::app {

class MapSaveVerdict final : public IWriteVerdict {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit MapSaveVerdict(const JoyMapEnds& ends) noexcept : ends_(ends) {}

    void on_written(bool durable) noexcept override {
        TASTY_SEAT_BODY(MapSaveVerdict);
        if (!durable)
            return;
        ends_.hotplug.request();
        ends_.input_ctrl.kick();
    }

private:
    const JoyMapEnds& ends_;
};

}
