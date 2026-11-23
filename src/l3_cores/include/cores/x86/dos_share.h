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
#include "cores/x86/dos_attr.h"
#include "cores/x86/dos_name83.h"
#include "cores/x86/dos_open_mode.h"
#include "cores/x86/dos_path.h"
#include "cores/x86/dos_share_reply.h"
#include "cores/x86/dos_share_request.h"
#include "cores/x86/dos_stamp.h"
#include "cores/x86/x86_share_key.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "svc/file.h"

namespace mister::cores::x86 {

class DosShare {
    TASTY_SEAT_RESIDENT(Io);

public:
    using Request = DosShareRequest;
    using Reply = DosShareReply;

    static constexpr infra::ByteOffset kRequestAt{0x0004};
    static constexpr infra::ByteOffset kPayloadAt{0x0008};
    static constexpr std::size_t kPageLength = 0x2000;
    static constexpr std::size_t kDataOffset = kRequestAt.bytes() + kPayloadAt.bytes();
    static constexpr std::size_t kDataLength = kPageLength - kDataOffset;
    static_assert(kDataOffset + kDataLength == kPageLength);

    static constexpr infra::ByteCount kHeaderSize{kPayloadAt.bytes()};
    static constexpr infra::ByteOffset kFuncAt{4};
    static constexpr infra::ByteOffset kResultAt{4};
    static constexpr infra::ByteOffset kLengthAt{0};

    [[nodiscard]] Ex<Request> decode(std::span<const std::byte> packet) const;

    [[nodiscard]] Ex<Reply> serve(Request& asked, HostFileSandbox& share, MailboxWindow& window);

    [[nodiscard]] infra::ByteCount encode(
        const Reply& answered,
        std::span<std::byte> packet_out
    ) const;

    void reset() noexcept;

private:
    struct Item {
        Name83 name{};
        DosStamp stamp{};
        proto::FileSize size{};
        DosAttr attr{};
    };

    struct Lock {
        DosToken token{};
        std::vector<Item> items{};
    };

    struct OpenFile {
        std::unique_ptr<svc::IFile> file{};
        DosOpenMode::Access access{};
    };

    [[nodiscard]] std::optional<std::string> find_path_(
        const HostFileSandbox& share,
        const DosPath& path
    ) const;
    [[nodiscard]] ShareLockKey add_lock_(DosToken token);
    [[nodiscard]] ShareFileKey take_file_key_();
    [[nodiscard]] Reply item_reply_(ShareLockKey key, DosCursor cursor);

    [[nodiscard]] Reply rmdir_(const Request& asked, HostFileSandbox& share);
    [[nodiscard]] Reply mkdir_(const Request& asked, HostFileSandbox& share);
    [[nodiscard]] Reply chdir_(const Request& asked, HostFileSandbox& share);
    [[nodiscard]] Reply close_(const Request& asked);
    [[nodiscard]] Reply read_(const Request& asked, const MailboxWindow& window);
    [[nodiscard]] Reply write_(const Request& asked, const MailboxWindow& window);
    [[nodiscard]] Reply disk_space_(const HostFileSandbox& share) const;
    [[nodiscard]] Reply get_attr_(const Request& asked, HostFileSandbox& share) const;
    [[nodiscard]] Reply rename_(const Request& asked, HostFileSandbox& share);
    [[nodiscard]] Reply delete_(const Request& asked, HostFileSandbox& share);
    [[nodiscard]] Reply open_(const Request& asked, HostFileSandbox& share);
    [[nodiscard]] Reply create_(const Request& asked, HostFileSandbox& share);
    [[nodiscard]] Reply sp_open_(const Request& asked, HostFileSandbox& share);
    [[nodiscard]] Reply find_first_(const Request& asked, HostFileSandbox& share);
    [[nodiscard]] Reply find_next_(const Request& asked);
    [[nodiscard]] Reply skf_mend_(const Request& asked) const;
    [[nodiscard]] DosStamp stamp_of_(
        const HostFileSandbox& share,
        std::string_view path
    ) const noexcept;
    [[nodiscard]] proto::FileSize size_of_(const HostFileSandbox& share, const DosPath& path) const;
    [[nodiscard]] static Reply qualify_() noexcept;
    [[nodiscard]] static Reply ok_() noexcept;
    [[nodiscard]] static Reply failed_(DosError error) noexcept;

    std::map<ShareLockKey, Lock> locks_{};
    std::map<ShareFileKey, OpenFile> files_{};
    ShareLockKey next_lock_{};
    ShareFileKey next_file_{ShareFileKey::kFirst};
};

}
