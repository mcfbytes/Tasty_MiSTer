// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"
#include "proto/user_io/poll_frame.h"

namespace mister::proto::link {

struct PollFrameRead {

    enum class Outcome : std::uint8_t { Faulted, Held, Taken };

    Outcome outcome = Outcome::Faulted;
    FioWordCount charged{};
    user_io::PollFrame frame{};
};

}
