// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/companion_host.h"
#include "app/mailbox_slot.h"
#include "cores/mailbox_port.h"
#include "infra/counter.h"
#include "infra/loan_channel.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "proto/mailbox_frame.h"
#include "proto/mailbox_poll.h"

namespace mister::app {

class MailboxRelay final : public cores::IMailboxPort {
    TASTY_SEAT_MEDIATOR(RT, Pcm);

public:
    using Chan = xthread::LoanChannel<MailboxSlot, kMailboxDepth, SeatTag::RT, SeatTag::Pcm>;

    static constexpr unsigned kReapPerRound = 2;

    explicit MailboxRelay(xthread::WakeFlag& servant_wake) noexcept : chan_(servant_wake) {}
    MailboxRelay(const MailboxRelay&) = delete;
    MailboxRelay& operator=(const MailboxRelay&) = delete;

    void service_rt(hal::ISpiTransport& link, proto::SpiFioQueue& queue,
                    const proto::MailboxFrameDecl& frame) noexcept override;

    void bind(proto::MailboxPoll poll, std::uint16_t gen) noexcept;

    void forget() noexcept;

    void serve_worker(CompanionHost& host) noexcept;

    void park_now() noexcept;
    [[nodiscard]] bool worker_idle() const noexcept { return chan_.outbound() == 0; }

    [[nodiscard]] bool polling() const noexcept { return polling_; }

    [[nodiscard]] std::uint32_t polls() const noexcept { return polls_.get(); }
    [[nodiscard]] std::uint32_t forwarded() const noexcept { return forwarded_.get(); }
    [[nodiscard]] std::uint32_t declines() const noexcept { return declines_.get(); }
    [[nodiscard]] std::uint32_t stale() const noexcept { return stale_.get(); }
    [[nodiscard]] std::uint32_t acts() const noexcept { return acts_.get(); }
    [[nodiscard]] std::uint32_t refusals() const noexcept { return refusals_.get(); }
    [[nodiscard]] std::uint32_t wire_faults() const noexcept { return wire_faults_.get(); }
    [[nodiscard]] Chan::Census census() const noexcept { return chan_.census(); }

    [[nodiscard]] Chan& channel_for_test() noexcept { return chan_; }

private:
    void write_(proto::SpiFioQueue& queue, const MailboxSlot& s) noexcept;

    Chan chan_;

    std::uint16_t last_head_ = 0;
    bool have_last_ = false;
    bool polling_ = false;
    std::uint16_t gen_ = 0;
    std::uint16_t round_ = 0;
    xthread::Counter polls_, forwarded_, declines_, stale_, acts_, refusals_, wire_faults_;
};

}  // namespace mister::app
