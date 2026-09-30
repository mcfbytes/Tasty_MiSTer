// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/error.h"

namespace mister::os {

Ex<int> run_sync(std::span<const char* const> argv, std::int32_t timeout_ms = 30'000);

Ex<void> run_detached(std::span<const char* const> argv);

}  // namespace mister::os
