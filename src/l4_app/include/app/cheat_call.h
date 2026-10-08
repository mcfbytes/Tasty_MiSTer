// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::cores {
class Core;
}

namespace mister::app {

struct CheatCall {
    cores::Core* core = nullptr;
    bool fio_held = false;
};

}  // namespace mister::app
