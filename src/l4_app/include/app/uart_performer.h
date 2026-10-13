// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/uart_handoff.h"
#include "app/uart_mode.h"
#include "infra/counter.h"
#include "infra/seat.h"

namespace mister::app {

class UartPerformer {
    TASTY_SEAT_RESIDENT(Ui);

public:
    void on(const UartHandoff::Mode& a);
    void on(const UartHandoff::MidiLink& a);
    void misrouted(const UartHandoff& h) noexcept;

    struct Stats {
        std::uint32_t script_spawns = 0;
        std::uint32_t script_failures = 0;
        std::uint32_t script_nonzero = 0;
        std::uint32_t handoff_misrouted = 0;
    };
    Stats stats() const noexcept {
        return {script_spawns_.get(), script_failures_.get(), script_nonzero_.get(),
                handoff_misrouted_.get()};
    }

private:
    xthread::Counter script_spawns_{}, script_failures_{}, script_nonzero_{}, handoff_misrouted_{};
};

}  // namespace mister::app
