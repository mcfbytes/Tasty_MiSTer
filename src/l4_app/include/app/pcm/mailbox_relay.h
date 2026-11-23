// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

#include "app/pcm/companion_host.h"
#include "app/pcm/mailbox_slot.h"
#include "app/round_words.h"
#include "cores/mailbox_port.h"
#include "infra/indexed_array.h"
#include "infra/xthread/counter.h"
#include "infra/xthread/loan_channel.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/wake_flag.h"
#include "proto/link/link.h"
#include "proto/disk_address.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_bus.h"
#include "proto/mailbox_poll.h"
#include "proto/mailbox_programme.h"
#include "proto/mailbox_select.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::app::pcm {

class MailboxRelay final : public cores::IMailboxPort {
    TASTY_SEAT_MEDIATOR(RT, Pcm);

public:
    using Chan = xthread::LoanChannel<MailboxSlot, kMailboxDepth, SeatTag::RT, SeatTag::Pcm>;

    static constexpr unsigned kReapPerRound = 2;

    static constexpr proto::FioWordCount kMaxRoundWords = proto::kMailboxPollWords
        + proto::kMailboxDirectWords;

    static constexpr proto::FioWordCount kMaxBusWords = proto::user_io::kCommandBeatWords
        + proto::DiskAddress::kWords
        + proto::kMailboxBusMaxWords;
    static_assert(
        kMaxBusWords == proto::FioWordCount{1179},
        "the opcode beat, two address words and one raw CD frame as words"
    );

    explicit MailboxRelay(xthread::WakeFlag& servant_wake) noexcept : chan_(servant_wake) {}

    MailboxRelay(const MailboxRelay&) = delete;
    MailboxRelay& operator=(const MailboxRelay&) = delete;

    void service_rt(
        proto::link::Link& link,
        proto::link::SpiFioQueue& queue,
        const proto::MailboxProgramme& programme,
        const os::IClock& clock
    ) noexcept override;

    void bind(proto::MailboxPoll poll, proto::CompanionBindGen gen) noexcept;

    void forget() noexcept;

    [[nodiscard]] proto::FioWordCount take_round_words() noexcept { return round_words_.take(); }

    void serve_worker(CompanionHost& host) noexcept;

    void park_now() noexcept;

    [[nodiscard]] bool worker_idle() const noexcept { return chan_.outbound() == 0 && !held_; }

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
    using Mirrors = infra::IndexedArray<
        std::optional<proto::ReplyWord>,
        proto::kMailboxSelects.size(),
        proto::MailboxSelectIndex>;
    static_assert(std::is_trivially_copyable_v<Mirrors::value_type>);

    struct RoundActs {
        bool wrote = false;
        bool direct_spent = false;
    };

    [[nodiscard]] RoundActs write_acts_(
        proto::link::Link& link,
        proto::link::SpiFioQueue& queue,
        const proto::MailboxProgramme& programme,
        const os::IClock& clock
    ) noexcept;
    void run_programme_(
        proto::link::Link& link,
        const proto::MailboxProgramme& programme,
        RoundActs acted
    ) noexcept;
    [[nodiscard]] bool repeat_(
        proto::link::Link& link,
        const proto::MailboxProgramme& programme,
        const proto::MailboxSelect& select,
        proto::user_io::PollFrame& frame
    ) noexcept;
    [[nodiscard]] bool faulted_() noexcept;
    [[nodiscard]] proto::MailboxAct answer_(
        cores::IMailboxServant& servant,
        MailboxSlot& slot
    ) noexcept;

    Chan chan_;

    std::optional<Chan::Job> held_{};
    bool held_awaited_ = false;

    Mirrors mirrors_{};

    std::optional<Chan::Loan> held_act_{};
    bool polling_ = false;
    bool bound_afresh_ = false;
    proto::CompanionBindGen gen_{};
    std::uint16_t round_ = 0;
    RoundWords round_words_{};
    xthread::Counter polls_, forwarded_, declines_, stale_, acts_, refusals_, wire_faults_;
    xthread::Counter none_acts_;
};

}
