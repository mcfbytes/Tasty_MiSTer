// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/core_input_facts.h"
#include "app/types.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"

namespace mister::app {

class CoreInputBinding final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Ends {
        CoreInputCell& facts;
        xthread::WakeFlag& joy_edge_reset;
        xthread::WakeFlag& hotplug;
        xthread::WakeFlag::Kicker input_ctrl;
    };

    explicit CoreInputBinding(Ends e) noexcept : e_(e) {}

    CoreInputBinding(const CoreInputBinding&) = delete;
    CoreInputBinding& operator=(const CoreInputBinding&) = delete;

    void on_core_loaded(const CoreInputFacts& f) noexcept;

    void on_core_detached() noexcept;

    void reset_joy_edges() noexcept { e_.joy_edge_reset.request(); }

    [[nodiscard]] CoreEdgeSeq core_edges() const noexcept { return core_edges_; }

private:
    Ends e_;

    CoreInputFacts last_{};

    CoreEdgeSeq core_edges_{};
};

}
