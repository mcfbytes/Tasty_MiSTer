// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>

#include "tasty_cli.h"

namespace mister::fw {

[[nodiscard]] constexpr bool tasty_boot_calls_handoff() noexcept { return false; }
[[nodiscard]] int tasty_run_owner(const TastyArgs& args);

[[nodiscard]] const std::atomic<int>& tasty_owner_stop_flag() noexcept;
void tasty_arm_owner_signals() noexcept;

void tasty_owe_home() noexcept;

[[nodiscard]] int tasty_stop_and_owe() noexcept;

[[nodiscard]] bool tasty_home_may_launch() noexcept;

}  // namespace mister::fw
