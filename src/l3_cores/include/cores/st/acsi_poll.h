// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "hal/types.h"
#include "proto/disk_poll_program.h"
#include "proto/disk_poll_step.h"
#include "proto/link/spi_fio_queue.h"
#include "proto/types.h"
#include "proto/user_io/condition.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::cores::st {

inline constexpr proto::ReplyCount kAcsiStateReplies{16};
inline constexpr proto::ReplyIndex kAcsiSelectReply{10};

inline constexpr proto::ReplyMask kAcsiPendingBit{0x0001};

inline constexpr auto kAcsiPollSteps = std::to_array<proto::DiskPollStep>({
    {.frame = proto::user_io::PollFrameShape{
         {.opcode = hal::LinkWord{0x0C},
          .kind = proto::user_io::TransferKind::AckedByte,
          .leading = kAcsiStateReplies,
          .guard =
              {.condition = proto::user_io::Condition::NonZero,
               .reply = kAcsiSelectReply,
               .mask = kAcsiPendingBit}}
     }},
});
inline constexpr proto::DiskPollProgram kAcsiPoll{kAcsiPollSteps};
static_assert(kAcsiPoll.max_words() <= proto::link::tightest_round_budget_words());

}
