// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "cores/host_file_server.h"
#include "cores/mailbox_req_id.h"
#include "cores/share_host.h"
#include "cores/x86/dos_share.h"
#include "cores/x86/dos_share_word.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "svc/vfs.h"

namespace mister::cores::x86 {

class ShareHost : public cores::IShareHost {
    TASTY_SEAT_RESIDENT(Io);

public:
    struct Tally {
        infra::Tally served{};
        infra::Tally partial{};
        infra::Tally refused{};
    };

    ShareHost(const svc::Vfs& vfs, std::string_view share_root) : server_(vfs, share_root) {}

    [[nodiscard]] Ex<void> bind(std::span<std::byte> page) override;

    void unbind() noexcept override;

    void reset() noexcept override;

    void serve() noexcept override;

    [[nodiscard]] bool bound() const noexcept { return !page_.empty(); }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

private:
    HostFileServer<DosShare> server_;
    std::span<std::byte> page_{};
    MailboxReqId seen_{};
    Tally tally_{};
};

}
