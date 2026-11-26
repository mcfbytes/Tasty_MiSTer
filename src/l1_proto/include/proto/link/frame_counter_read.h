// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"
#include "proto/link/link.h"
#include "infra/prelude.h"
#include "proto/frame_count.h"
#include "proto/types.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::proto::link {

class FrameCounterRead {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::uint32_t kWrapMask = 0xFFu;

    static constexpr FioWordCount kReadWords{1};

    static constexpr hal::LinkWord kSupportedBit{0x100u};

    [[nodiscard]] Ex<FrameCount> read(Link& link);

    [[nodiscard]] std::uint32_t reads() const noexcept { return reads_; }

private:
    std::uint32_t reads_ = 0;
};

}
