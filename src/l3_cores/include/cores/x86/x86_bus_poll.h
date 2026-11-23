// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "cores/ide/ide_channel_row.h"
#include "cores/ide/ide_task_file.h"
#include "hal/types.h"
#include "infra/affine.h"
#include "infra/indexed_array.h"
#include "proto/disk_address.h"
#include "proto/disk_poll_program.h"
#include "proto/disk_poll_step.h"
#include "proto/link/spi_fio_queue.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"
#include "proto/user_io/condition.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::cores::x86 {

inline constexpr proto::ReplyMask kIde0Request{0x0007};
inline constexpr proto::ReplyMask kIde1Request{0x0038};
inline constexpr proto::ReplyMask kFdcRead{0x0040};
inline constexpr proto::ReplyMask kFdcWrite{0x0080};
inline constexpr proto::ReplyMask kFdcRequest{0x00C0};
inline constexpr proto::ReplyMask kBusRequests{0x00FF};

inline constexpr proto::ReplyMask kCddaRequest{0x0400};

inline constexpr proto::ReplyMask kBusAndCddaRequests{0x04FF};
static_assert(
    kBusRequests.within(kBusAndCddaRequests) && kCddaRequest.within(kBusAndCddaRequests),
    "ao486's head guards every lane its RTL answers"
);

inline constexpr proto::DiskAddress kIde0Base{0xF000};
inline constexpr proto::DiskAddress kIde1Base{0xF100};
inline constexpr proto::DiskAddress kFdcBase{0xF200};

inline constexpr proto::DiskAddress kCddaPort{0xF300};
static_assert(
    kCddaPort == kIde1Base + proto::DiskRegisterCount{0x200},
    "the CD-DA FIFO sits past IDE 1's register block"
);

inline constexpr proto::DiskAddress kRtcBase{0xF400};

inline constexpr std::size_t kFdcDrives = 2;
inline constexpr proto::DiskRegisterCount kFdcDriveStride{0x80};

inline constexpr auto kFdcDriveBase = [] {
    infra::IndexedArray<proto::DiskAddress, kFdcDrives, proto::DiskUnit> bases{};
    for (std::uint8_t i = 0; i < kFdcDrives; ++i)
        bases[proto::DiskUnit{i}] = kFdcBase + kFdcDriveStride * std::uint32_t{i};
    return bases;
}();
static_assert(kFdcDriveBase[proto::DiskUnit{1}] == proto::DiskAddress{0xF280});

inline constexpr proto::ReplyCount kIdeTaskWords = ide::IdeTaskFile::kWords;
inline constexpr proto::ReplyCount kFdcParamWords{2};

inline constexpr proto::user_io::PollFrameShape kBusRequestRead{
    {.opcode = proto::word(proto::UioOp::DmaSdio),
     .keeps_command_reply = true,
     .kind = proto::user_io::TransferKind::AckedWord,
     .guard = {
         .condition = proto::user_io::Condition::NonZero,
         .reply = proto::ReplyIndex{},
         .mask = kBusRequests
     }}
};

inline constexpr proto::user_io::PollFrameShape kBusAndCddaRequestRead{
    {.opcode = proto::word(proto::UioOp::DmaSdio),
     .keeps_command_reply = true,
     .kind = proto::user_io::TransferKind::AckedWord,
     .guard = {
         .condition = proto::user_io::Condition::NonZero,
         .reply = proto::ReplyIndex{},
         .mask = kBusAndCddaRequests
     }}
};

consteval proto::user_io::PollFrameShape register_read(
    proto::DiskAddress at,
    proto::ReplyCount words
) {
    return proto::user_io::PollFrameShape{
        {.opcode = proto::word(proto::UioOp::DmaRead),
         .address = at,
         .kind = proto::user_io::TransferKind::AckedWord,
         .leading = words}
    };
}

inline constexpr proto::DiskPollStep kBusRequestStep{.frame = kBusRequestRead};
inline constexpr proto::DiskPollStep kBusAndCddaRequestStep{.frame = kBusAndCddaRequestRead};
inline constexpr proto::DiskPollStep kIde0Step{
    .frame = register_read(kIde0Base, kIdeTaskWords),
    .gate = kIde0Request
};
inline constexpr proto::DiskPollStep kIde1Step{
    .frame = register_read(kIde1Base, kIdeTaskWords),
    .gate = kIde1Request
};
inline constexpr proto::DiskPollStep kFdcStep{
    .frame = register_read(kFdcBase, kFdcParamWords),
    .gate = kFdcRequest
};

inline constexpr ide::IdeChannelRow kIde0Channel{
    .base = kIde0Base,
    .request = kIde0Request,
    .task_file_at = proto::ReplyIndex{1},
    .units = {.master = proto::DiskUnit{2}, .slave = proto::DiskUnit{3}},
    .mounts = ide::MountRule::DiskOnly
};
inline constexpr ide::IdeChannelRow kIde1Channel{
    .base = kIde1Base,
    .request = kIde1Request,
    .task_file_at = proto::ReplyIndex{7},
    .units = {.master = proto::DiskUnit{4}, .slave = proto::DiskUnit{5}},
    .mounts = ide::MountRule::DiskOnly
};

inline constexpr auto kTwoChannelSteps = std::to_array<proto::DiskPollStep>(
    {kBusAndCddaRequestStep, kIde0Step, kIde1Step, kFdcStep}
);
inline constexpr auto kOneChannelSteps = std::to_array<proto::DiskPollStep>(
    {kBusRequestStep, kIde0Step, kFdcStep}
);
inline constexpr auto kNoChannelSteps = std::to_array<proto::DiskPollStep>(
    {kBusRequestStep, kFdcStep}
);

inline constexpr proto::DiskPollProgram kTwoChannelBusPoll{kTwoChannelSteps};
inline constexpr proto::DiskPollProgram kOneChannelBusPoll{kOneChannelSteps};
inline constexpr proto::DiskPollProgram kNoChannelBusPoll{kNoChannelSteps};

static_assert(kTwoChannelBusPoll.max_words() <= proto::link::tightest_round_budget_words());

static_assert(
    kFdcRequest.within(kBusRequests)
    && kFdcRead.within(kFdcRequest)
    && kFdcWrite.within(kFdcRequest)
    && kIde0Request.within(kBusRequests)
    && kIde1Request.within(kBusRequests)
);
static_assert(
    !kCddaRequest.within(kBusRequests),
    "the PCXT family's head cannot see the CD-DA bit"
);

[[nodiscard]] constexpr proto::ReplyIndex fdc_params_at(const proto::DiskPollProgram& p) noexcept {
    return infra::affine::end_of<proto::ReplyIndex>(p.kept() - kFdcParamWords);
}

static_assert(kIde0Channel.task_file_at == proto::ReplyIndex{} + proto::ReplyCount{1});
static_assert(kIde0Channel.task_file_at + kIdeTaskWords == kIde1Channel.task_file_at);
static_assert(kIde0Channel.task_file_at + kIdeTaskWords == fdc_params_at(kOneChannelBusPoll));
static_assert(kIde1Channel.task_file_at + kIdeTaskWords == fdc_params_at(kTwoChannelBusPoll));

}
