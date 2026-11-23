// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/types.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::proto {

struct DiskPollStep {
    user_io::PollFrameShape frame;

    ReplyMask gate{};
};

}
