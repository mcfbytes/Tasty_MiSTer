// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "cores/host_services.h"
#include "cores/mailbox_rows.h"
#include "infra/seat.h"
#include "proto/mailbox_frame_decl.h"

namespace mister::cores {

class MailboxCarriage final : public IMailboxRows {
    TASTY_SEAT_RESIDENT(RT);

public:
    MailboxCarriage(hal::ISpiTransport& link, IMailboxPort& port, proto::SpiFioQueue& queue,
                    proto::MailboxFrameDecl frame) noexcept
        : link_(link), port_(port), queue_(queue), frame_(frame) {}

    [[nodiscard]] static std::optional<MailboxCarriage> granted(
        const HostServices& h, proto::MailboxFrameDecl frame) noexcept {
        if (!h.mailbox || !h.fio_queue) return std::nullopt;
        return std::optional<MailboxCarriage>{std::in_place, h.link, *h.mailbox, *h.fio_queue,
                                              frame};
    }

    void service_mailbox_tick() noexcept override;

private:
    hal::ISpiTransport& link_;
    IMailboxPort& port_;
    proto::SpiFioQueue& queue_;
    proto::MailboxFrameDecl frame_;
};

}  // namespace mister::cores
