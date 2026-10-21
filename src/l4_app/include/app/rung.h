// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/file_tx_level.h"
#include "app/link_tx_channel.h"
#include "app/rung_edges.h"
#include "cores/ladder_host.h"

namespace mister::app {

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
