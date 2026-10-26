// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/osd_target.h"

namespace mister::app {

struct UiRoundEdge {
    bool pumped = false;
    proto::OsdTarget target = proto::OsdTarget::All;
};

}
