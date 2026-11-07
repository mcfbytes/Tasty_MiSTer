// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "app/file_tx_level.h"
#include "app/link_tx_channel.h"
#include "app/rung_edges.h"
#include "cores/ladder_host.h"
#include "proto/types.h"

namespace mister::app {

inline constexpr std::uint16_t kStockFileTxChunk = 4096;

struct EchoRate {
    std::chrono::nanoseconds floor{};
    std::chrono::nanoseconds per_mib{};
};

[[nodiscard]] constexpr std::chrono::nanoseconds echo_bound(
    proto::FileSize bytes,
    EchoRate rate
) noexcept {
    return rate.floor + bytes.mib_share_of(rate.per_mib);
}

class Rung {
public:
    enum class Pass : std::uint8_t { Waiting, Moved, Done, Failed };

    virtual ~Rung() = default;

    [[nodiscard]] virtual bool wants_pass(
        const LinkTxChannel<SeatTag::Main>& inbox,
        const FileTxLevelCell& level
    ) const noexcept = 0;

    [[nodiscard]] virtual Pass step(const RungEdges& edges, cores::ILadderHost& owner) = 0;

protected:
    Rung() = default;
    Rung(const Rung&) = default;
    Rung& operator=(const Rung&) = default;
    Rung(Rung&&) = default;
    Rung& operator=(Rung&&) = default;
};

}
