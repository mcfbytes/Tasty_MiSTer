// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/cheat_sink.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/prelude.h"
#include "hal/spi_transport.h"

namespace mister::proto::link {
class SpiFioQueue;
}

namespace mister::cores {

class FioCheatSink final : public ICheatSink {
    TASTY_SEAT_RESIDENT(RT);

public:
    FioCheatSink(hal::ISpiTransport& link, proto::link::SpiFioQueue& queue) noexcept
        : link_(&link),
          queue_(&queue) {}

    [[nodiscard]] Ex<void> apply(std::span<const std::uint8_t> table, CheatUnit unit) override;

    [[nodiscard]] std::uint32_t sends() const noexcept {
        TASTY_SEAT_BODY(FioCheatSink);
        return sends_;
    }

    [[nodiscard]] std::uint64_t bytes_sent() const noexcept {
        TASTY_SEAT_BODY(FioCheatSink);
        return bytes_;
    }

private:
    hal::ISpiTransport* link_;
    proto::link::SpiFioQueue* queue_;
    std::uint32_t sends_ = 0;
    std::uint64_t bytes_ = 0;
};

}
