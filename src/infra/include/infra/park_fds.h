// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <utility>

#include "infra/error.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"

namespace mister::xthread {

class ParkFds {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static Ex<ParkFds> create(WakeFlag& wake) noexcept {
        if (wake.fd() < 0) {
            if (auto r = wake.open_fd(); !r) return std::unexpected(r.error());
        }
        WakeFlag stop;
        if (auto r = stop.open_fd(); !r) return std::unexpected(r.error());
        return ParkFds{wake, std::move(stop)};
    }

    ParkFds(ParkFds&&) noexcept = default;
    ParkFds(const ParkFds&) = delete;
    ParkFds& operator=(const ParkFds&) = delete;
    ParkFds& operator=(ParkFds&&) = delete;

    [[nodiscard]] WakeFlag& wake() const noexcept { return *wake_; }

    [[nodiscard]] int stop_fd() const noexcept { return stop_.fd(); }

    [[nodiscard]] WakeFlag take_stop() && noexcept { return std::move(stop_); }

private:
    ParkFds(WakeFlag& wake, WakeFlag&& stop) noexcept : wake_(&wake), stop_(std::move(stop)) {}

    WakeFlag* wake_;
    WakeFlag stop_;
};

}  // namespace mister::xthread
