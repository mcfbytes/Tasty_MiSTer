// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/cheat_sink.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "hal/spi_transport.h"
#include "proto/spi_fio_queue.h"

namespace mister::cores {

class FioCheatSink final : public ICheatSink {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit FioCheatSink(hal::ISpiTransport& link, proto::SpiFioQueue* queue = nullptr) noexcept
        : link_(&link), queue_(queue) {}

    [[nodiscard]] Ex<void> apply(std::span<const std::uint8_t> table, std::uint32_t unit) override;

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
    proto::SpiFioQueue* queue_;
    std::uint32_t sends_ = 0;
    std::uint64_t bytes_ = 0;
};

}  // namespace mister::cores
