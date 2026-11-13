// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <span>

#include "tasty_runtime.h"

namespace mister::fw {

[[nodiscard]] std::optional<std::span<const char* const>> tasty_invocation(
    std::span<const char* const> argv
) noexcept;

struct ArgvArea {
    int argc;
    char** argv;
    char** env;
};

[[nodiscard]] int run_tasty(
    std::span<const char* const> argv,
    std::optional<ArgvArea> area = std::nullopt,
    const TastyRuntime& rt = {}
);

}
