// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/staged_frame.h"

namespace mister::app {

struct StagedFrameRoutes {
    template <class A>
    static constexpr bool routed = true;
};

}
