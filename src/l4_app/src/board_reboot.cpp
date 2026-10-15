// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/board_reboot.h"

#include <unistd.h>

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

    if (handoff_) (void)handoff_->write_reboot_flag(!cold);
    if (!reset_) return std::unexpected(Error{Errc::bridge_state, ERR_SITE(), 0});
    reset_->board_reset();
    return {};
}

}  // namespace mister::app
