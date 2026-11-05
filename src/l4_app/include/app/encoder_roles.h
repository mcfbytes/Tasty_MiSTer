// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/cheat_call.h"
#include "app/save_flush.h"
#include "infra/opt_ref.h"

namespace mister::app {

class ICheatApply;
class ICoreOptionActs;

struct EncoderRoles {
    std::optional<SaveFlush> saves{};
    infra::OptRef<ICheatApply> cheats{};
    CheatCall cheat_call{};
    infra::OptRef<ICoreOptionActs> acts{};
};

}
