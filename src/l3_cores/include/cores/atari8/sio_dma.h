// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/atari8/atari8_address.h"
#include "cores/atari8/atari8_wire.h"
#include "cores/atari8/sio_types.h"
#include "infra/byte_count.h"
#include "infra/message_sum.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_run.h"

namespace mister::cores::atari8 {

inline constexpr infra::ByteCount kMostUpload{48};

[[nodiscard]] constexpr Atari8Address host_at(HostAddress address) noexcept {
    return Atari8Address::kHostMemory.after(address.from_zero());
}

[[nodiscard]] constexpr proto::MailboxAct dma_write(
    std::span<std::uint8_t, proto::kMailboxActBytes> bytes,
    Atari8Address at,
    std::span<const std::byte> payload
) noexcept {
    const std::size_t n = std::min(payload.size(), bytes.size());
    std::ranges::transform(payload.first(n), bytes.begin(), [](std::byte b) {
        return std::to_integer<std::uint8_t>(b);
    });
    return infra::make<proto::MailboxAct>(proto::MailboxAct::AddressedDownload{
        .address = at.aux(),
        .index = wire::kDmaIndex,
        .len = proto::MailboxActLen::clamped(infra::ByteCount{n})
    });
}

[[nodiscard]] constexpr proto::MailboxAct dma_written(
    Atari8Address at,
    infra::ByteCount length
) noexcept {
    return infra::make<proto::MailboxAct>(proto::MailboxAct::AddressedDownload{
        .address = at.aux(),
        .index = wire::kDmaIndex,
        .len = proto::MailboxActLen::clamped(length)
    });
}

[[nodiscard]] constexpr proto::MailboxAct dma_read(
    Atari8Address at,
    infra::ByteCount length
) noexcept {
    return infra::make<proto::MailboxAct>(proto::MailboxAct::AddressedUpload{
        .address = at.aux(),
        .index = wire::kDmaIndex,
        .len = proto::MailboxActLen::clamped(std::min(length, kMostUpload))
    });
}

}
