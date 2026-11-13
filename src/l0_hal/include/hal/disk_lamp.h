// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "hal/spi_bus.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"

namespace mister::hal {

class DiskLamp final {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::chrono::milliseconds kHold{50};

    static constexpr std::chrono::milliseconds kTick{1};

    explicit DiskLamp(SpiBus& bus) noexcept : bus_(bus) {}

    void note() noexcept { left_ = kHold; }

    void on_round(bool tick, const xthread::WakeFlag& off_seat) noexcept {
        if (seen_.take(off_seat))
            note();
        if (tick && left_ > decltype(left_)::zero())
            left_ -= kTick;
        const bool lit = left_ > decltype(left_)::zero();
        if (bus_.gpo_shadow().has(gpo::kDiskLed) != lit)
            bus_.set_disk_led(lit);
    }

private:
    SpiBus& bus_;
    xthread::WakeFlag::Cursor seen_{};
    std::chrono::milliseconds left_{};
};

}
