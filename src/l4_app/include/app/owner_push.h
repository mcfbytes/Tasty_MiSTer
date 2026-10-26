// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/link_tx_channel.h"
#include "app/owner_tally.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::app {

class ISleeper;

template <class Op>
[[nodiscard]] inline bool push_dropping(
    LinkTxChannel<SeatTag::Main>& inbox,
    OwnerTally& tally,
    const Op& op
) noexcept {
    if (inbox.push(op))
        return true;
    ++tally.op_drops;
    return false;
}

template <class Op>
[[nodiscard]] inline bool push_refusing(
    LinkTxChannel<SeatTag::Main>& inbox,
    OwnerTally& tally,
    const Op& op
) noexcept {
    if (inbox.push(op))
        return true;
    ++tally.order_refusals;
    return false;
}

inline constexpr unsigned kPushRetries = 50;

[[nodiscard]] bool push_gating(
    LinkTxChannel<SeatTag::Main>& inbox,
    OwnerTally& tally,
    infra::OptRef<ISleeper> sleeper,
    const proto::LinkOp& op
);

}
