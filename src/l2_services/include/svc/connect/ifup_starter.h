// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "os/child_process.h"
#include "os/connect/if_name.h"

namespace mister::svc::connect {

class IfupStarter {
    TASTY_SEAT_RESIDENT(Connect);

public:
    [[nodiscard]] Ex<os::ChildProcess> start(os::connect::IfName ifname) const;
    [[nodiscard]] Ex<os::ChildProcess> stop(os::connect::IfName ifname) const;
};

}
