// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/servant_id.h"
#include "os/clock.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

[[nodiscard]] ServantSet make_servants(const svc::Vfs& vfs, const os::IClock& clock);

}
