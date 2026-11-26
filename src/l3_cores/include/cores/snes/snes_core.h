// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "cores/core.h"
#include "cores/mailbox_carriage.h"
#include "cores/stream_load_host.h"
#include "infra/prelude.h"
#include "infra/seat.h"

namespace mister::cores::snes {

class SnesCore final : public Core {
    TASTY_SEAT_RESIDENT(RT);

public:
    SnesCore(const CoreProfile& p, const HostServices& h);

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;

    [[nodiscard]] std::uint64_t on_last_tx_bytes() const noexcept override {
        return stream_.bytes();
    }

    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;

    [[nodiscard]] infra::OptRef<IStreamLoad> on_stream_load() noexcept override {
        return infra::OptRef<IStreamLoad>{stream_};
    }

    [[nodiscard]] infra::OptRef<IMailboxRows> on_mailbox_rows() noexcept override {
        return mailbox_ ? infra::OptRef<IMailboxRows>{*mailbox_} : infra::OptRef<IMailboxRows>{};
    }

    StreamLoadHost stream_;

    std::optional<MailboxCarriage> mailbox_;
};

}
