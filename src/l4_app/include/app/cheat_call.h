// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/opt_ref.h"

namespace mister::cores {
class Core;
}

namespace mister::app {

struct CheatCall {
    infra::OptRef<cores::Core> core{};
    bool fio_held = false;
};

}  // namespace mister::app
