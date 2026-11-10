// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::proto {

struct MouseDelta {
    int dx = 0;
    int dy = 0;
    int wheel = 0;
    friend constexpr bool operator==(MouseDelta, MouseDelta) = default;
};

}
