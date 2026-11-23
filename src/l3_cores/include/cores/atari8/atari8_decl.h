// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "cores/atari8/atari8_status.h"
#include "cores/atari8/atari8_status2.h"
#include "cores/atari8/atari8_wire.h"
#include "cores/atari8/sio_wire.h"
#include "proto/mailbox_programme.h"
#include "proto/mailbox_run.h"
#include "proto/mailbox_select.h"
#include "proto/types.h"
#include "proto/user_io/condition.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::cores::atari8 {

inline constexpr proto::ReplyIndex kStatusReply{0};
inline constexpr proto::MailboxSelectIndex kStatusSelect{0};

inline constexpr proto::ReplyMask kHotkeyMask{0x0007};
static_assert(kHotkeyMask.wire_word() == Atari8Status::kHotkeys);
inline constexpr proto::ReplyMask kComputerStatusMask{0x87C7};
static_assert(
    kComputerStatusMask.wire_word() == Atari8Status::kHotkeys.with(Atari8Status::kDriveOptions)
);

inline constexpr proto::user_io::PollFrameShape kStatusFrame{
    {.opcode = wire::word(wire::Opcode::GetRegister),
     .kind = proto::user_io::TransferKind::AckedWord,
     .leading = proto::ReplyCount{1},
     .guard =
         {.condition = proto::user_io::Condition::MaskedChangedSinceLast,
          .reply = kStatusReply,
          .mask = kHotkeyMask},
     .exchanged = wire::select_word(wire::ReadRegister::Status1)}
};

inline constexpr auto kAtari8Selects = std::to_array<proto::MailboxSelect>({
    {.shape = kStatusFrame, .relent_at_bind = true},
});

inline constexpr proto::ReplyMask kTapeFullMask{0x8000};
static_assert(kTapeFullMask.wire_word() == Atari8Status2::kTapeFull);

inline constexpr proto::user_io::PollFrameShape kTapeNotFull{
    {.opcode = wire::word(wire::Opcode::GetRegister),
     .kind = proto::user_io::TransferKind::AckedWord,
     .leading = proto::ReplyCount{1},
     .guard =
         {.condition = proto::user_io::Condition::Zero,
          .reply = proto::ReplyIndex{0},
          .mask = kTapeFullMask},
     .exchanged = wire::select_word(wire::ReadRegister::Status2)}
};

inline constexpr proto::MailboxProgramme kAtari8Mailbox{kAtari8Selects};

inline constexpr proto::user_io::PollFrameShape kComputerStatusFrame{
    {.opcode = wire::word(wire::Opcode::GetRegister),
     .kind = proto::user_io::TransferKind::AckedWord,
     .leading = proto::ReplyCount{1},
     .guard =
         {.condition = proto::user_io::Condition::MaskedChangedSinceLast,
          .reply = kStatusReply,
          .mask = kComputerStatusMask},
     .exchanged = wire::select_word(wire::ReadRegister::Status1)}
};

inline constexpr proto::MailboxSelectIndex kRxSelect{1};
inline constexpr proto::ReplyIndex kRxStatusReply{0};
inline constexpr proto::MailboxRunGuard kTxGuard{0};
inline constexpr proto::MailboxRunGuard kTapeGuard{1};
inline constexpr auto kAtari800Selects = std::to_array<proto::MailboxSelect>({
    {.shape = kComputerStatusFrame, .relent_at_bind = true},
    {.shape = wire::kRxFrame},
});
inline constexpr auto kAtari800Guards = std::to_array<proto::user_io::PollFrameShape>({
    wire::kTxRoomGuard,
    kTapeNotFull,
});
inline constexpr proto::MailboxProgramme kAtari800Mailbox{kAtari800Selects, kAtari800Guards};

}
