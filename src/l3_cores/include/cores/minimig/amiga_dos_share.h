// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cores/host_file_sandbox.h"
#include "cores/mailbox_window.h"
#include "cores/minimig/share_key.h"
#include "cores/minimig/share_reply.h"
#include "cores/minimig/share_request.h"
#include "cores/minimig/share_wire.h"
#include "infra/byte_count.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::cores::minimig {

class AmigaDosShare {
    TASTY_SEAT_RESIDENT(Io);

public:
    using Request = ShareRequest;
    using Reply = ShareReply;
    static constexpr std::size_t kDataOffset = kShareDataAt.bytes();
    static constexpr std::size_t kDataLength = kShareDataBytes.size();

    [[nodiscard]] Ex<Request> decode(std::span<const std::byte> packet) const;
    [[nodiscard]] Ex<Reply> serve(Request& asked, HostFileSandbox& share, MailboxWindow& window);

    [[nodiscard]] infra::ByteCount encode(
        const Reply& answered,
        std::span<std::byte> packet_out
    ) const;

    void reset() noexcept;

private:
    using SharePath = std::string;

    struct Lock {
        std::optional<SharePath> path{};
        std::vector<std::string> listing{};
    };

    struct OpenFile {
        std::unique_ptr<svc::IFile> file{};
        SharePath path{};
        proto::FileOffset at{};
    };

    [[nodiscard]] std::optional<SharePath> find_path_(
        const HostFileSandbox& share,
        ShareLockKey key,
        std::string_view name
    ) const;
    [[nodiscard]] ShareLockKey add_lock_(std::optional<SharePath> path);
    [[nodiscard]] ShareFileKey take_file_key_();
    [[nodiscard]] bool has_locks_(const SharePath& path) const noexcept;

    [[nodiscard]] Reply locate_(const Request& asked, const HostFileSandbox& share);
    [[nodiscard]] Reply copy_lock_(const Request& asked);
    [[nodiscard]] Reply parent_(const Request& asked);
    [[nodiscard]] Reply examine_(const Request& asked, const HostFileSandbox& share);
    [[nodiscard]] Reply examine_fh_(const Request& asked, const HostFileSandbox& share) const;
    [[nodiscard]] Reply find_(const Request& asked, HostFileSandbox& share);
    [[nodiscard]] Reply read_(const Request& asked, const MailboxWindow& window);
    [[nodiscard]] Reply write_(const Request& asked, const MailboxWindow& window);
    [[nodiscard]] Reply seek_(const Request& asked);
    [[nodiscard]] Reply delete_(const Request& asked, HostFileSandbox& share) const;
    [[nodiscard]] Reply rename_(const Request& asked, HostFileSandbox& share) const;
    [[nodiscard]] Reply create_dir_(const Request& asked, HostFileSandbox& share);
    [[nodiscard]] Reply info_(const Request& asked, const HostFileSandbox& share) const;
    [[nodiscard]] Reply same_lock_(const Request& asked) const;

    std::map<ShareLockKey, Lock> locks_{};
    std::map<ShareFileKey, OpenFile> files_{};
    ShareLockKey next_lock_{1};
    ShareFileKey next_file_{1};
};

}
