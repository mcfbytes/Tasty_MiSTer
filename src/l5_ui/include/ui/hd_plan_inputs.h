// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "ui/build_env.h"
#include "ui/page_id.h"
#include "ui/types.h"

namespace mister::app {
class RecentGames;
}

namespace mister::proto {
class ItemTable;
}

namespace mister::ui {

class OsdStylePreference;
struct CoreVerbs;
class NowPlaying;
struct PageContext;
struct StatusView;

struct HdPlanInputs final {
    TASTY_SEAT_RESIDENT(Ui);
    PageContext& ctx;
    OsdStylePreference& style;
    const proto::ItemTable& items;
    const StatusView& status;
    const CoreVerbs& verbs;
    const NowPlaying& now_playing;
    const app::RecentGames& recent_games;
    BuildEnv env;
    ItemId focus;
    PageId classic;
    bool front_end;
};

}
