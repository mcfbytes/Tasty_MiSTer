// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/atari8/atari8_status.h"
#include "cores/atari8/sio_drives.h"
#include "proto/mailbox_act.h"

namespace mister::cores::atari8 {

struct SioTerms {
    SioDrives& drives;
    Atari8Status status{};
    proto::MailboxAct::NotBefore now{};
};

}
