// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "cores/disk_servant.h"
#include "cores/servant_id.h"
#include "infra/opt_ref.h"
#include "infra/xthread/wake_flag.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

[[nodiscard]] std::unique_ptr<IDiskServant> make_disk_servant(
    const svc::Vfs& vfs,
    infra::OptRef<xthread::WakeFlag> activity
);

[[nodiscard]] ServantSet make_servants(const svc::Vfs& vfs);

}
