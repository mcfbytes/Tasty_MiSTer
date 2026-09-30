// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "tasty_cli.h"

namespace mister::fw {

[[nodiscard]] constexpr bool tasty_boot_calls_handoff() noexcept { return false; }
[[nodiscard]] int tasty_run_owner(const TastyArgs& args);

}  // namespace mister::fw
