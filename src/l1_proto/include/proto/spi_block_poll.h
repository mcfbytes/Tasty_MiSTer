// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "hal/spi_transport.h"
#include "proto/block_geometry.h"
#include "proto/block_geometry_hook.h"
#include "proto/sd_request.h"
#include "proto/slot_roles.h"
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
    struct Wiring {
        infra::OptRef<const std::uint64_t> slot0_file_bytes{};
        infra::OptRef<const SlotRoleTable> roles{};
    };

    SpiBlockPoll() noexcept = default;
    explicit SpiBlockPoll(Wiring w) noexcept : wiring_(w) {}

    struct Decode {
        bool decoded = false;
        SdRequest req;
        std::uint16_t ack = 0;
        std::uint32_t block_size = 512;
        std::uint32_t window_blocks = 0;
    };

    [[nodiscard]] Ex<Decode> poll(hal::ISpiTransport& link,
                                  std::optional<SdStatusWord> known = std::nullopt);

    const DecodeCounters& diagnostics() const noexcept { return diag_; }

    [[nodiscard]] Ex<void> send_config(hal::ISpiTransport& link);

private:
    [[nodiscard]] IBlockGeometry* geometry_for_(SlotIndex slot) const noexcept;

    Wiring wiring_{};
    DecodeCounters diag_{};
};

}  // namespace mister::proto
