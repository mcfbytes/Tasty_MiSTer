// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "cores/sidecar_payload.h"

namespace mister::cores {

struct SidecarPlan {
    std::vector<SidecarPayload> items{};

    [[nodiscard]] bool empty() const noexcept { return items.empty(); }
};

}
