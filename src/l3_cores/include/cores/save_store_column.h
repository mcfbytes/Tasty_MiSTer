// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/xthread/wake_flag.h"
#include "svc/storage_backend.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

using MakeSaveStore =
    std::unique_ptr<svc::IStorageBackend> (*)(const svc::Vfs&, infra::OptRef<xthread::WakeFlag>);

}
