// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "cores/minimig/akiko_wire.h"
#include "cores/minimig/cd_bridge.h"
#include "cores/minimig/cd_status.h"
#include "proto/mailbox_gate.h"
#include "proto/mailbox_programme.h"
#include "proto/mailbox_select.h"
#include "proto/types.h"
#include "proto/user_io/condition.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::cores::minimig::cd_decl {

inline constexpr proto::MailboxSelectIndex kStatusSelect{0};
inline constexpr proto::MailboxSelectIndex kAkikoDrainSelect{1};
inline constexpr proto::MailboxSelectIndex kSectorCounterSelect{2};
inline constexpr proto::MailboxSelectIndex kCdtvDrainSelect{3};

inline constexpr proto::ReplyIndex kFirstReply{0};

inline constexpr proto::ReplyMask kCounterMask{0x00FF};

inline constexpr proto::ReplyCount kCdtvRepeats{12};

inline constexpr proto::user_io::PollFrameShape kStatusFrame{
    {.opcode = cd_bridge::kStatus,
     .keeps_command_reply = true,
     .guard = {
         .condition = proto::user_io::Condition::MaskedChangedSinceLast,
         .reply = kFirstReply,
         .mask = CdStatus::kDrives
     }}
};

inline constexpr proto::user_io::PollFrameShape kAkikoDrainFrame{
    {.opcode = cd_bridge::kRead,
     .address = cd_bridge::kAkikoCommand,
     .kind = proto::user_io::TransferKind::AckedByte,
     .leading = proto::ReplyCount{1},
     .head_lengths = akiko::drain_lengths()}
};

inline constexpr proto::user_io::PollFrameShape kSectorCounterFrame{
    {.opcode = cd_bridge::kRead,
     .address = cd_bridge::kAkikoSector,
     .kind = proto::user_io::TransferKind::AckedByte,
     .leading = proto::ReplyCount{1},
     .guard = {
         .condition = proto::user_io::Condition::MaskedChangedSinceLast,
         .reply = kFirstReply,
         .mask = kCounterMask
     }}
};

inline constexpr proto::user_io::PollFrameShape kCdtvDrainFrame{
    {.opcode = cd_bridge::kRead,
     .address = cd_bridge::kCdtvCommand,
     .kind = proto::user_io::TransferKind::AckedByte,
     .leading = proto::ReplyCount{1}}
};

inline constexpr auto kCdSelects = std::to_array<proto::MailboxSelect>({
    {.shape = kStatusFrame, .relent_at_bind = true, .relent_after_act = true},
    {.shape = kAkikoDrainFrame,
     .gate = proto::MailboxGate{.on = kStatusSelect, .mask = CdStatus::kAkikoRequest},
     .pops = true},
    {.shape = kSectorCounterFrame,
     .relent_at_bind = true,
     .gate = proto::MailboxGate{.on = kStatusSelect, .mask = CdStatus::kAkikoSectorRequest},
     .relent_after_act = true},
    {.shape = kCdtvDrainFrame,
     .gate = proto::MailboxGate{.on = kStatusSelect, .mask = CdStatus::kCdtvRequest},
     .pops = true,
     .repeats = kCdtvRepeats},
});
inline constexpr proto::MailboxProgramme kCdMailbox{kCdSelects};

}
