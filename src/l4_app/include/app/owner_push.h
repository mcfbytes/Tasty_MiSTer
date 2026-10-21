// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/link_tx_channel.h"
#include "app/owner_tally.h"
#include "infra/seat.h"

namespace mister::app {

template <class Op>
[[nodiscard]] inline bool push_posted(
    LinkTxChannel<SeatTag::Main>& inbox,
    OwnerTally& tally,
    const Op& op
) noexcept {
    if (!inbox.push(op))
        return false;
    ++tally.ops_posted;
    return true;
}

template <class Op>
[[nodiscard]] inline bool push_dropping(
    LinkTxChannel<SeatTag::Main>& inbox,
    OwnerTally& tally,
    const Op& op
) noexcept {
    if (push_posted(inbox, tally, op))
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
    if (push_posted(inbox, tally, op))
        return true;
    ++tally.order_refusals;
    return false;
}

}
