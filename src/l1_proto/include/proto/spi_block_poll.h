// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/seat.h"
#include "hal/spi_transport.h"
#include "proto/block_geometry.h"
#include "proto/block_geometry_hook.h"
#include "proto/sd_request.h"
#include "proto/types.h"

namespace mister::proto {

struct DecodeCounters {
    std::uint32_t stock_requests = 0;
    std::uint32_t config_requests = 0;
    std::uint32_t oversize_requests = 0;
};

class SpiBlockPoll {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Decode {
        bool decoded = false;
        SdRequest req;
        std::uint16_t ack = 0;
        std::uint32_t block_size = 512;
        std::uint32_t window_blocks = 0;
    };

    [[nodiscard]] Ex<Decode> poll(hal::ISpiTransport& link,
                                  std::optional<SdStatusWord> known = std::nullopt);

    void set_geometry_hook(IBlockGeometry* hook) noexcept { geometry_ = hook; }

    void bind_slot0_file_bytes(const std::uint64_t* p) noexcept { slot0_file_bytes_ = p; }

    const DecodeCounters& diagnostics() const noexcept { return diag_; }

    [[nodiscard]] Ex<void> send_config(hal::ISpiTransport& link);

private:
    IBlockGeometry* geometry_ = nullptr;
    const std::uint64_t* slot0_file_bytes_ = nullptr;
    DecodeCounters diag_{};
};

}  // namespace mister::proto
