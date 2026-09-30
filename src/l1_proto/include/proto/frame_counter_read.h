// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/spi_transport.h"
#include "proto/frame_count.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::proto {

class FrameCounterRead {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::uint16_t kOpcode = 0x42;

    static constexpr std::uint32_t kWrapMask = 0xFFu;

    static constexpr std::uint32_t kReadWords = 1;

    [[nodiscard]] Ex<FrameCount> read(hal::ISpiTransport& link);

    [[nodiscard]] std::uint32_t reads() const noexcept { return reads_; }

private:
    std::uint32_t reads_ = 0;
};

}  // namespace mister::proto
