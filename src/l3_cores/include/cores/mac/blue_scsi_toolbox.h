// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "cores/host_file_sandbox.h"
#include "cores/mac/blue_scsi_reply.h"
#include "cores/mac/blue_scsi_wire.h"
#include "cores/mac/listing_index.h"
#include "cores/mailbox_window.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/dir_entry.h"
#include "svc/file.h"

namespace mister::cores::mac {

class BlueScsiToolbox {
    TASTY_SEAT_RESIDENT(Io);

public:
    enum class Op : std::uint8_t { List, Get, Count, SendPrep, SendData, SendEnd };

    struct Request {
        Op op = Op::Count;
        ListingIndex index{};
        proto::FileOffset at{};
        infra::ByteCount want{};
        std::span<const std::byte> head{};
        std::span<const std::byte> tail{};
    };

    using Reply = BlueScsiReply;
    static constexpr std::size_t kDataOffset = 0;
    static constexpr std::size_t kDataLength = kBlueScsiReplyBytes;

    [[nodiscard]] Ex<Request> decode(std::span<const std::byte> request) const;
    [[nodiscard]] Ex<Reply> serve(Request& asked, HostFileSandbox& share, MailboxWindow& window);
    [[nodiscard]] infra::ByteCount encode(
        const Reply& answered,
        std::span<std::byte> reply_out
    ) const;

    void close() noexcept;

private:
    [[nodiscard]] std::vector<svc::DirEntry> names_(const HostFileSandbox& share) const;
    [[nodiscard]] Reply count_(const HostFileSandbox& share, std::span<std::byte> out);
    [[nodiscard]] Reply list_(const HostFileSandbox& share, std::span<std::byte> out);
    [[nodiscard]] Reply get_(
        const Request& asked,
        const HostFileSandbox& share,
        std::span<std::byte> out
    );
    [[nodiscard]] Reply send_prep_(const Request& asked, HostFileSandbox& share);
    [[nodiscard]] Reply send_data_(const Request& asked);
    [[nodiscard]] Reply send_end_();

    [[nodiscard]] bool close_();

    std::unique_ptr<svc::IFile> file_;
    bool uploading_ = false;

    std::vector<std::byte> chunk_;
};

}
