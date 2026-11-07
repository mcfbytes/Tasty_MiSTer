// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/owner_push.h"

#include <unistd.h>

#include "app/board_ops.h"
#include "proto/link_op.h"

namespace mister::app {

using WholeMs = std::chrono::duration<unsigned, std::milli>;
using WholeUs = std::chrono::duration<::useconds_t, std::micro>;

void sleep_ms(infra::OptRef<ISleeper> sleeper, std::chrono::milliseconds ms) noexcept {
    if (sleeper) {
        sleeper->sleep_ms(std::chrono::duration_cast<WholeMs>(ms).count());
    } else {
        ::usleep(std::chrono::duration_cast<WholeUs>(ms).count());
    }
}

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
