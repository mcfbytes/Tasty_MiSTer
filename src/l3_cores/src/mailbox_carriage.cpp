// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mailbox_carriage.h"

#include "cores/mailbox_port.h"

namespace mister::cores {

void MailboxCarriage::service_mailbox_tick() noexcept {
    TASTY_SEAT_BODY(MailboxCarriage);
    port_.service_rt(link_, queue_, frame_);
}

}
