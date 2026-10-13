// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/cheat_call.h"
#include "infra/opt_ref.h"

namespace mister::app {

class ISaveFlush;
class ICheatApply;
class ICoreOptionActs;

struct EncoderRoles {
    infra::OptRef<ISaveFlush> saves{};
    infra::OptRef<ICheatApply> cheats{};
    CheatCall cheat_call{};
    infra::OptRef<ICoreOptionActs> acts{};
};

}  // namespace mister::app
