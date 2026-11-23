// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "cores/host_file_server.h"
#include "cores/minimig/amiga_dos_share.h"
#include "cores/minimig/share_request_word.h"
#include "cores/share_host.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "svc/vfs.h"

namespace mister::cores::minimig {

class ShareHost final : public cores::IShareHost {
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
    HostFileServer<AmigaDosShare> server_;
    std::span<std::byte> page_{};
    MailboxReqId seen_{0};
    Tally tally_{};
};

}
