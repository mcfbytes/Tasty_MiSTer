// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/quiesce.h"
#include "infra/seat.h"

namespace mister::app {

class QuiesceAck {
    TASTY_SEAT_RESIDENT(RT);

public:
    void poll(QuiesceChannel& ch) noexcept;

    void step(QuiesceChannel& ch, bool held_back) noexcept;

    [[nodiscard]] bool asked() const noexcept { return asked_; }

    [[nodiscard]] bool parked() const noexcept { return parked_; }
    [[nodiscard]] std::uint32_t gen() const noexcept { return gen_; }

    [[nodiscard]] bool engaged() const noexcept { return asked_ || parked_; }

    void release() noexcept;

    void withdraw() noexcept;

    [[nodiscard]] std::uint32_t ack_drops() const noexcept { return drops_; }

private:
    bool asked_ = false;
    bool parked_ = false;
    std::uint32_t gen_ = 0;
    std::uint32_t drops_ = 0;
};

}  // namespace mister::app
