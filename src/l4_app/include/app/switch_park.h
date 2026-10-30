// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/park_receipt.h"
#include "infra/opt_ref.h"
#include "infra/pause_latch.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"

namespace mister::app {

class SwitchPark {
    TASTY_SEAT_MEDIATOR(Main, RT);

public:
    explicit SwitchPark(xthread::WakeFlag& main_wake) noexcept
        : latch_(xthread::Observed{}, infra::OptRef<xthread::WakeFlag>{main_wake}) {}

    explicit SwitchPark(xthread::Polled) noexcept
        : latch_(xthread::Observed{}, infra::OptRef<xthread::WakeFlag>{}) {}

    SwitchPark(const SwitchPark&) = delete;
    SwitchPark& operator=(const SwitchPark&) = delete;

    void ask(xthread::PauseGen g) noexcept { latch_.pause(g); }

    void end() noexcept { latch_.resume(); }

    [[nodiscard]] xthread::PauseGen asked() const noexcept { return latch_.asked(); }

    [[nodiscard]] std::optional<ParkReceipt> answered(xthread::PauseGen g) const noexcept {
        if (g == 0 || !latch_.paused_at(g))
            return std::nullopt;
        return ParkReceipt{g};
    }

    template <class Body>
    [[nodiscard]] bool observe(xthread::PauseLatch::Observer& seen, Body& b) noexcept {
        return latch_.observe(seen, b);
    }

private:
    xthread::PauseLatch latch_;
};

}
