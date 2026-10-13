// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <variant>

#include "cores/send_bracket.h"
#include "cores/stage_window.h"

namespace mister::cores {

using ManifestAct = std::variant<StageWindow, SendBracket>;

}
