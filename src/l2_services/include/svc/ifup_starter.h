// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "infra/seat.h"
#include "os/child_process.h"
#include "os/if_name.h"

namespace mister::svc {

class IfupStarter {
    TASTY_SEAT_RESIDENT(Connect);

public:
    [[nodiscard]] Ex<os::ChildProcess> start(os::IfName ifname) const;
    [[nodiscard]] Ex<os::ChildProcess> stop(os::IfName ifname) const;
};

}
