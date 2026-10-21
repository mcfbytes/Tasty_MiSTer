// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/board_ops.h"
#include "app/link_tx_channel.h"
#include "app/owner_tally.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "os/deadline.h"
#include "proto/link_op.h"

namespace mister::hal {
class IBootHandoff;
}

namespace mister::app {

class BoardReboot final {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr unsigned kSettleMs = 500;

    BoardReboot(
        const os::IClock& clock,
        LinkTxChannel<SeatTag::Main>& inbox,
        OwnerTally& owner_tally,
        infra::OptRef<hal::IBootHandoff> handoff,
        infra::OptRef<IBoardReset> reset,
        infra::OptRef<ISleeper> sleeper
    ) noexcept
        : clock_(clock),
          inbox_(inbox),
          owner_tally_(owner_tally),
          handoff_(handoff),
          reset_(reset),
          sleeper_(sleeper) {}

    [[nodiscard]] Ex<void> reboot(bool cold);

    [[nodiscard]] std::uint32_t reboots() const noexcept { return reboots_; }

    struct OwedReboot {
        bool cold = true;
        bool ordered = false;
        std::uint16_t seq = 0;
    };

    struct Tally {
        std::uint32_t reboots_settled = 0;
        std::uint32_t reboot_drain_expiries = 0;
    };

    static constexpr std::int64_t kDrainBoundNs = 5'000'000'000;

    void settle(bool cold);

    [[nodiscard]] bool watch();

    void finish();

    [[nodiscard]] const std::optional<OwedReboot>& owed() const noexcept { return owed_; }

    [[nodiscard]] const os::Deadline& due() const noexcept { return due_; }

    [[nodiscard]] std::uint16_t seq() const noexcept { return seq_; }

    [[nodiscard]] bool failed() const noexcept { return failed_; }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

private:
    bool order_refusable_(const proto::LinkOp::RebootNow& op) noexcept;
    bool repush_(const proto::LinkOp::RebootNow& op) noexcept;

    const os::IClock& clock_;
    LinkTxChannel<SeatTag::Main>& inbox_;
    OwnerTally& owner_tally_;
    infra::OptRef<hal::IBootHandoff> handoff_{};
    infra::OptRef<IBoardReset> reset_{};
    infra::OptRef<ISleeper> sleeper_{};
    std::uint32_t reboots_ = 0;
    std::uint16_t seq_ = 0;
    std::optional<OwedReboot> owed_{};
    os::Deadline due_ = os::Deadline::immediate();
    bool failed_ = false;
    Tally tally_{};
};

}
