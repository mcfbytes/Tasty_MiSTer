// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "cores/host_services.h"
#include "cores/mailbox_rows.h"
#include "infra/seat.h"
#include "proto/mailbox_programme.h"

namespace mister::cores {

class MailboxCarriage final : public IMailboxRows {
    TASTY_SEAT_RESIDENT(RT);

public:
    MailboxCarriage(
        hal::ISpiTransport& link,
        IMailboxPort& port,
        proto::link::SpiFioQueue& queue,
        const os::IClock& clock,
        proto::MailboxProgramme programme
    ) noexcept
        : link_(link),
          port_(port),
          queue_(queue),
          clock_(clock),
          programme_(programme) {}

    [[nodiscard]] static std::optional<MailboxCarriage> granted(
        const HostServices& h,
        proto::MailboxProgramme programme
    ) noexcept {
        if (!h.mailbox)
            return std::nullopt;
        return std::optional<MailboxCarriage>{
            std::in_place,
            h.link,
            *h.mailbox,
            h.fio_queue,
            h.clock,
            programme
        };
    }

    void service_mailbox_tick() noexcept override;

private:
    hal::ISpiTransport& link_;
    IMailboxPort& port_;
    proto::link::SpiFioQueue& queue_;
    const os::IClock& clock_;
    proto::MailboxProgramme programme_;
};

}
