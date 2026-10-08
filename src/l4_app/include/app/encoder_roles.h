// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/cheat_call.h"

namespace mister::app {

class ISaveFlush;
class ICheatApply;
class ICoreOptionActs;

struct EncoderRoles {
    ISaveFlush* saves = nullptr;
    ICheatApply* cheats = nullptr;
    CheatCall cheat_call{};
    ICoreOptionActs* acts = nullptr;
};

}  // namespace mister::app
