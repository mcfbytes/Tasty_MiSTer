// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/mailbox_servant.h"
#include "cores/minimig/cd_bridge.h"
#include "infra/byte_count.h"
#include "infra/message_sum.h"
#include "proto/disk_address.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_bus.h"
#include "proto/types.h"
#include "svc/disc/track.h"

namespace mister::cores::minimig::cd_bus {

using BulkBytes = IMailboxServant::BulkBytes;
using NotBefore = proto::MailboxAct::NotBefore;

static_assert(proto::kMailboxBulkBytes == svc::disc::kRawFrameBytes);

[[nodiscard]] inline proto::MailboxAct write_bytes(
    proto::DiskAddress at,
    std::span<const std::byte> bytes,
    BulkBytes bulk,
    NotBefore after = {}
) noexcept {
    std::ranges::copy(bytes, bulk.begin());
    return infra::make<proto::MailboxAct>(proto::MailboxAct::BusWrite{
        .not_before = after,
        .address = at,
        .opcode = cd_bridge::kWriteOp,
        .len = proto::MailboxBusLen::widened(infra::ByteCount{bytes.size()})
    });
}

[[nodiscard]] inline proto::MailboxAct write_words(
    proto::DiskAddress at,
    std::span<const std::byte> bytes,
    BulkBytes bulk
) noexcept {
    std::ranges::copy(bytes, bulk.begin());
    return infra::make<proto::MailboxAct>(proto::MailboxAct::BusWrite{
        .address = at,
        .opcode = cd_bridge::kWriteOp,
        .len = proto::MailboxBusLen::packed(infra::ByteCount{bytes.size()})
    });
}

[[nodiscard]] inline proto::MailboxAct write_swapped(
    proto::DiskAddress at,
    std::span<const std::byte> samples,
    BulkBytes bulk
) noexcept {
    constexpr std::size_t kSampleBytes = 2;
    for (std::size_t i = 0; i + 1 < samples.size(); i += kSampleBytes) {
        bulk[i] = samples[i + 1];
        bulk[i + 1] = samples[i];
    }
    return infra::make<proto::MailboxAct>(proto::MailboxAct::BusWrite{
        .address = at,
        .opcode = cd_bridge::kWriteOp,
        .len = proto::MailboxBusLen::packed(infra::ByteCount{samples.size()})
    });
}

[[nodiscard]] inline proto::MailboxAct read_bytes(
    proto::DiskAddress at,
    infra::ByteCount n
) noexcept {
    return infra::make<proto::MailboxAct>(proto::MailboxAct::BusRead{
        .address = at,
        .opcode = cd_bridge::kReadOp,
        .len = proto::MailboxBusLen::widened(n)
    });
}

[[nodiscard]] inline proto::MailboxAct wait_until(NotBefore at) noexcept {
    return infra::make<proto::MailboxAct>(proto::MailboxAct::CommandRun{.not_before = at});
}

}
