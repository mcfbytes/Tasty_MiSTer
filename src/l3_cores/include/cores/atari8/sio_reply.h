// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <optional>

#include "cores/atari8/sio_types.h"
#include "infra/byte_count.h"

namespace mister::cores::atari8 {

struct SioReply {
    infra::ByteCount bytes{};
    bool success = true;
    bool respond = true;
    std::optional<SioDivisor> divisor{};

    std::optional<std::chrono::microseconds> timed{};
};

}
