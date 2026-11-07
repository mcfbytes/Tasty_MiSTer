// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "hal/spi_transport.h"
#include "proto/block_backing.h"
#include "proto/block_geometry.h"
#include "proto/block_roles.h"
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
    struct Wiring {
        hal::ISpiTransport& link;
        infra::OptRef<const std::uint64_t> slot0_file_bytes{};
    };

    explicit SpiBlockPoll(Wiring w) noexcept : wiring_(w) {}

    struct Decode {
        bool decoded = false;
        SdRequest req;
        SectorAck ack{};
        BlockSize block_size{kDefaultBlockBytes};
        BlockCount window_blocks{};
    };

    [[nodiscard]] Ex<Decode> poll(
        IBlockRoles& roles,
        const SlotServers& servers,
        std::optional<SdStatusWord> known = std::nullopt
    );

    const DecodeCounters& diagnostics() const noexcept { return diag_; }

    [[nodiscard]] Ex<void> send_config();

private:
    Wiring wiring_;
    DecodeCounters diag_{};
};

}
