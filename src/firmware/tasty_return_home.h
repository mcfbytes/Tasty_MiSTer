// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "tasty_home_lane.h"
#include "tasty_owner_lock.h"

namespace mister::fw {

class ReturnHome {
    TASTY_SEAT_RESIDENT(Main);

public:
    explicit ReturnHome(const HomeLane::Bind& bind = {}) noexcept;
    ReturnHome(const ReturnHome&) = delete;
    ReturnHome& operator=(const ReturnHome&) = delete;
    ~ReturnHome() noexcept;

    const TastyOwnerLock& adopt(TastyOwnerLock lock) noexcept;

private:
    std::optional<TastyOwnerLock> lock_{};
};

}
