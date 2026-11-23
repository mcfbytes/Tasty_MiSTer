// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/minimig/a2065_command.h"
#include "cores/minimig/a2065_window.h"
#include "infra/seat.h"

namespace mister::cores::minimig {

class A2065CommandRing {
    TASTY_SEAT_RESIDENT(Nic);

public:
    static constexpr A2065CommandCount kDrainMax = A2065Window::kRingEntries;

    struct Pending {
        A2065CommandCount entries{};
        bool overran = false;
    };

    explicit A2065CommandRing(const A2065Window& window) noexcept : window_(window) {}

    void resync() noexcept { read_ = window_.write_index(); }

    [[nodiscard]] Pending pending() noexcept {
        const A2065CommandSeq written = window_.write_index();
        const A2065CommandCount behind = written - read_;
        if (behind <= kDrainMax)
            return Pending{.entries = behind};
        read_ = written - kDrainMax;
        return Pending{.entries = kDrainMax, .overran = true};
    }

    [[nodiscard]] A2065Command take() noexcept {
        const A2065Command c = window_.command(read_);
        read_ += kOne;
        return c;
    }

    [[nodiscard]] A2065CommandSeq read_index() const noexcept { return read_; }

private:
    static constexpr A2065CommandCount kOne{1};

    const A2065Window& window_;
    A2065CommandSeq read_{};
};

}
