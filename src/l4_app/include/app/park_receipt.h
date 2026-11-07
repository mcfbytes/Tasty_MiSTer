// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/pause_latch.h"

namespace mister::app {

class ParkReceipt {
public:
    ParkReceipt(const ParkReceipt&) = delete;
    ParkReceipt& operator=(const ParkReceipt&) = delete;
    ParkReceipt(ParkReceipt&&) noexcept = default;
    ParkReceipt& operator=(ParkReceipt&&) noexcept = default;
    ~ParkReceipt() = default;

    [[nodiscard]] xthread::PauseGen gen() const noexcept { return gen_; }

private:
    friend class SwitchPark;

    explicit ParkReceipt(xthread::PauseGen at_gen) noexcept : gen_(at_gen) {}

    xthread::PauseGen gen_;
};

}
