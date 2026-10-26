// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/owner_push.h"

#include <unistd.h>

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

        if (sleeper) {
            sleeper->sleep_ms(1);
        } else {
            ::usleep(1000u);
        }
    }
    ++tally.op_drops;
    return false;
}

}
