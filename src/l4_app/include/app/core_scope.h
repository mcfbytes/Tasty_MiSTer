// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/types.h"

namespace mister::app {

struct CoreScope {
    proto::BindGeneration gen{};
    friend constexpr bool operator==(CoreScope, CoreScope) = default;
};

}  // namespace mister::app
