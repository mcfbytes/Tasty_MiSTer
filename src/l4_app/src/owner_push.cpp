// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/owner_push.h"

#include <chrono>

#include "app/board_ops.h"
#include "proto/link_op.h"

namespace mister::app {

bool push_gating(
    LinkTxChannel<SeatTag::Main>& inbox,
    OwnerTally& tally,
    infra::OptRef<ISleeper> sleeper,
    const proto::LinkOp& op
) {
    for (unsigned t = 0; t < kPushRetries; ++t) {
        if (inbox.push(op))
            return true;

        sleep_ms(sleeper, std::chrono::milliseconds{1});
    }
    ++tally.op_drops;
    return false;
}

}
