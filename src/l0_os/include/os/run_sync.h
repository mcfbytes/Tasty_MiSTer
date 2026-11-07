// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <span>

#include "infra/error.h"
#include "os/child_process.h"

namespace mister::os {

[[nodiscard]] Ex<ChildProcessExit> run_sync(
    std::span<const char* const> argv,
    std::chrono::milliseconds timeout = std::chrono::seconds{30}
);

[[nodiscard]] Ex<void> run_detached(std::span<const char* const> argv);

}
