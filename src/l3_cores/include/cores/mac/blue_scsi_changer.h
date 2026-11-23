// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "cores/host_file_sandbox.h"
#include "cores/mac/blue_scsi_reply.h"
#include "cores/mac/blue_scsi_wire.h"
#include "cores/mac/listing_index.h"
#include "cores/mailbox_window.h"
#include "cores/remount_asked.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/dir_entry.h"

namespace mister::cores::mac {

class BlueScsiChanger {
    TASTY_SEAT_RESIDENT(Io);

public:
    enum class Op : std::uint8_t { List, SetNext, Count };

    struct Request {
        Op op = Op::Count;
        ListingIndex index{};
    };

    using Reply = BlueScsiReply;
    static constexpr std::size_t kDataOffset = 0;
    static constexpr std::size_t kDataLength = kBlueScsiReplyBytes;

    BlueScsiChanger(proto::SlotIndex disc_slot, infra::OptRef<RemountCell> remount) noexcept
        : disc_slot_(disc_slot),
          remount_(remount) {}

    [[nodiscard]] Ex<Request> decode(std::span<const std::byte> request) const;
    [[nodiscard]] Ex<Reply> serve(Request& asked, HostFileSandbox& discs, MailboxWindow& window);
    [[nodiscard]] infra::ByteCount encode(
        const Reply& answered,
        std::span<std::byte> reply_out
    ) const;

    void close() noexcept {}

private:
    [[nodiscard]] std::vector<svc::DirEntry> discs_(const HostFileSandbox& discs) const;

    proto::SlotIndex disc_slot_;
    infra::OptRef<RemountCell> remount_;
};

}
