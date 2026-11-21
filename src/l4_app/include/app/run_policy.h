// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/mount_queue.h"
#include "app/types.h"
#include "infra/fixed_str.h"
#include "infra/prelude.h"

namespace mister::app {

struct RunPolicy {
    using RememberedFiles = MountQueue::RememberedFiles;

    std::optional<VsyncAdjust> force_vsync{};
    std::optional<bool> replay_ini{};
    FixedStr<64, StrFit::Reject> save_root{};
    RememberedFiles remembered = RememberedFiles::AsStock;
};

}
