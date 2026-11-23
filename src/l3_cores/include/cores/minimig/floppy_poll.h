// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "hal/types.h"
#include "proto/disk_poll_program.h"
#include "proto/disk_poll_step.h"
#include "proto/link/spi_fio_queue.h"
#include "proto/link_sel.h"
#include "proto/types.h"
#include "proto/user_io/condition.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::cores::minimig {

inline constexpr proto::ReplyIndex kFloppyRequestReply{0};
inline constexpr proto::ReplyIndex kFloppySyncReply{1};
inline constexpr proto::ReplyIndex kFloppyCountReply{2};
inline constexpr proto::ReplyCount kFloppyReplies{3};

inline constexpr proto::ReplyMask kFloppyReadBit{0x0100};
inline constexpr proto::ReplyMask kFloppyWriteBit{0x0200};
inline constexpr proto::ReplyMask kFloppyRequestBits{0x0300};
inline constexpr proto::ReplyMask kFloppyTrackByte{0x00FF};

inline constexpr proto::ReplyMask kFloppyReadTag{0x01FF};
inline constexpr proto::ReplyMask kFloppyWriteTag{0x02FF};
static_assert(
    kFloppyReadTag.wire_word() == kFloppyReadBit.wire_word().with(kFloppyTrackByte.wire_word())
    && kFloppyWriteTag.wire_word() == kFloppyWriteBit.wire_word().with(kFloppyTrackByte.wire_word())
);

inline constexpr auto kFloppyPollSteps = std::to_array<proto::DiskPollStep>({
    {.frame = proto::user_io::PollFrameShape{
         {.opcode = hal::SpiWord{},
          .keeps_command_reply = true,
          .kind = proto::user_io::TransferKind::AckedWord,
          .leading = kFloppyReplies - proto::ReplyCount{1},
          .guard =
              {.condition = proto::user_io::Condition::NonZero,
               .reply = kFloppyRequestReply,
               .mask = kFloppyRequestBits},
          .select = proto::LinkSel::Fpga}
     }},
});
inline constexpr proto::DiskPollProgram kFloppyPoll{kFloppyPollSteps};
static_assert(kFloppyPoll.kept() == kFloppyReplies);
static_assert(kFloppyPoll.max_words() <= proto::link::tightest_round_budget_words());

}
