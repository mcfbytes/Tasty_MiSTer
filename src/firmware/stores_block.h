// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/cheat_store.h"
#include "app/name_config.h"
#include "app/played_history.h"
#include "app/recents_store.h"
#include "app/ui_write_lane.h"
#include "boot_parts.h"
#include "infra/seat.h"
#include "svc/config_snapshot.h"

namespace mister::fw {

class StoresBlock {
    TASTY_SEAT_EXEMPT(boot);

public:
    StoresBlock(const BootParts& plat, app::CheatBlobCell& blob, app::UiWriteLane& writes)
        : recents_{plat.vfs, writes, false},
          played_{plat.vfs, writes},
          names_{plat.vfs, app::NameConfig::card_cid_path(plat.kernel.sd_block)},
          cheats_{plat.vfs, blob} {}

    StoresBlock(const StoresBlock&) = delete;
    StoresBlock& operator=(const StoresBlock&) = delete;

    void latch(const svc::ConfigSnapshot& c) noexcept { recents_.set_enabled(c.recents != 0); }

    app::RecentsStore recents_;
    app::PlayedHistory played_;
    app::NameConfig names_;
    app::CheatStore cheats_;
};

}
