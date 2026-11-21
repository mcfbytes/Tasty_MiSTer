// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/xthread/wake_flag.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

struct ServantGrant {
    const svc::Vfs& vfs;
    infra::OptRef<xthread::WakeFlag> activity{};
};

}
