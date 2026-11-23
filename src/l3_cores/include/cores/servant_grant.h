// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/remount_asked.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/xthread/wake_flag.h"
#include "os/clock.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

struct ServantGrant {
    const svc::Vfs& vfs;
    infra::OptRef<xthread::WakeFlag> activity{};
    infra::OptRef<const os::IClock> clock{};
    infra::OptRef<RemountCell> remount{};
};

}
