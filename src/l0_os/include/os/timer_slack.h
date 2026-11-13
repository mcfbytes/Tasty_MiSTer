// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "infra/error.h"

namespace mister::os {

inline constexpr std::chrono::nanoseconds kMinTimerSlack{1};

[[nodiscard]] [[nodiscard]] Ex<void> set_timer_slack(std::chrono::nanoseconds slack) noexcept;

[[nodiscard]] [[nodiscard]] Ex<void> restore_default_timer_slack() noexcept;

}
