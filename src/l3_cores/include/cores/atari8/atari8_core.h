// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "cores/atari8/atari8_machine.h"
#include "cores/atari8/cart_stream_load.h"
#include "cores/core.h"
#include "cores/mailbox_carriage.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/mailbox_programme.h"

namespace mister::cores::atari8 {

class Atari8Core final : public Core {
    TASTY_SEAT_RESIDENT(RT);

public:
    Atari8Core(
        const CoreProfile& p,
        const HostServices& h,
        proto::MailboxProgramme mailbox,
        Atari8Machine machine
    )
        : Core(p, h),
          stream_(machine),
          mailbox_(MailboxCarriage::granted(h, mailbox)) {}

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt&) override { return {}; }

    std::uint64_t on_last_tx_bytes() const noexcept override { return stream_.host().bytes(); }

    svc::Crc32 on_last_tx_crc() const noexcept override { return stream_.host().crc(); }

    [[nodiscard]] infra::OptRef<IStreamLoad> on_stream_load() noexcept override {
        return infra::OptRef<IStreamLoad>{stream_};
    }

    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override {
        return stream_.host().mount(slot, p);
    }

    [[nodiscard]] infra::OptRef<IMailboxRows> on_mailbox_rows() noexcept override {
        return mailbox_ ? infra::OptRef<IMailboxRows>{*mailbox_} : infra::OptRef<IMailboxRows>{};
    }

    CartStreamLoad stream_;
    std::optional<MailboxCarriage> mailbox_;
};

}
