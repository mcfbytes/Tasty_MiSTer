// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/board_reboot.h"
#include "app/owner_push.h"

#include <unistd.h>

#include <chrono>

#include "hal/boot_handoff.h"

namespace mister::app {

Ex<void> BoardReboot::reboot(bool cold) {
    TASTY_SEAT_BODY(BoardReboot);
    ++reboots_;
    ::sync();
    if (sleeper_) {
        sleeper_->sleep_ms(kSettleMs);
    } else {
        ::usleep(static_cast<::useconds_t>(kSettleMs) * 1000u);
    }

    if (handoff_)
        (void)handoff_->write_reboot_flag(!cold);
    if (!reset_)
        return std::unexpected(Error{Errc::bridge_state, ERR_SITE(), 0});
    reset_->board_reset();
    return {};
}

bool BoardReboot::order_refusable_(const proto::LinkOp::RebootNow& op) noexcept {
    TASTY_SEAT_BODY(BoardReboot);
    return push_refusing(inbox_, owner_tally_, op);
}

bool BoardReboot::repush_(const proto::LinkOp::RebootNow& op) noexcept {
    TASTY_SEAT_BODY(BoardReboot);
    return push_posted(inbox_, owner_tally_, op);
}

void BoardReboot::settle(bool cold) {
    TASTY_SEAT_BODY(BoardReboot);

    if (owed_)
        return;
    const std::uint16_t seq = ++seq_;
    const proto::LinkOp::RebootNow quiesce{.cold = cold, .quiesce = true, .seq = seq};
    owed_ = OwedReboot{.cold = cold, .ordered = order_refusable_(quiesce), .seq = seq};
    due_ = os::Deadline::in(clock_, std::chrono::nanoseconds{kDrainBoundNs});
}

bool BoardReboot::watch() {
    TASTY_SEAT_BODY(BoardReboot);
    if (!owed_)
        return false;
    if (!owed_->ordered) {

        owed_->ordered = repush_(
            proto::LinkOp::RebootNow{.cold = owed_->cold, .quiesce = true, .seq = owed_->seq}
        );
    }
    if (!due_.expired(clock_))
        return true;

    ++tally_.reboot_drain_expiries;
    finish();
    return false;
}

void BoardReboot::finish() {
    TASTY_SEAT_BODY(BoardReboot);
    const bool cold = owed_ ? owed_->cold : true;
    owed_.reset();
    ++tally_.reboots_settled;
    (void)reboot(cold);

    (void)order_refusable_(proto::LinkOp::RebootNow{.cold = cold, .quiesce = false});
    failed_ = true;
}

}
