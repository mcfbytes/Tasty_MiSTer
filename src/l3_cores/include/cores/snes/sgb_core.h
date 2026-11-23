// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "cores/core_support.h"
#include "cores/mailbox_carriage.h"
#include "cores/stream_load_host.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/mailbox_programme.h"

namespace mister::cores::snes {

class SgbCore final : public Core {
    TASTY_SEAT_RESIDENT(RT);

public:
    SgbCore(const CoreProfile& p, const HostServices& h, proto::MailboxProgramme mailbox)
        : Core(p, h),
          mailbox_(MailboxCarriage::granted(h, mailbox)) {}

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt&) override { return {}; }

    std::uint64_t on_last_tx_bytes() const noexcept override { return stream_.bytes(); }

    svc::Crc32 on_last_tx_crc() const noexcept override { return stream_.crc(); }

    [[nodiscard]] infra::OptRef<IStreamLoad> on_stream_load() noexcept override {
        return infra::OptRef<IStreamLoad>{stream_};
    }

    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override {
        return stream_.mount(slot, p);
    }

    [[nodiscard]] infra::OptRef<IMailboxRows> on_mailbox_rows() noexcept override {
        return mailbox_ ? infra::OptRef<IMailboxRows>{*mailbox_} : infra::OptRef<IMailboxRows>{};
    }

    StreamLoadHost stream_;
    std::optional<MailboxCarriage> mailbox_;
};

}
