// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "cores/core_window_decl.h"
#include "cores/snes/msu_wire.h"
#include "proto/mailbox_programme.h"
#include "proto/mailbox_select.h"
#include "proto/uio_opcodes.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::cores::snes {

inline constexpr proto::user_io::PollFrameShape kSnesMailboxFrame{
    {.opcode = proto::word(proto::UioOp::CdGet),
     .keeps_command_reply = true,
     .kind = proto::user_io::TransferKind::AckedWord,
     .guarded = msu::kPayloadReplies,
     .guard = {
         .condition = proto::user_io::Condition::MaskedChangedSinceLast,
         .reply = msu::kCounterReply,
         .mask = msu::kCounterMask
     }}
};

inline constexpr auto kSnesMailboxSelects = std::to_array<proto::MailboxSelect>(
    {{.shape = kSnesMailboxFrame}}
);
inline constexpr proto::MailboxProgramme kSnesMailbox{kSnesMailboxSelects};

inline constexpr auto kSnesWindows = std::to_array<CoreWindowDecl>({
    {.region = {msu::kTrackOffset, 0u, "snes-msu-data"}, .feeds_pcm = false},
});

}
