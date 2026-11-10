// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/register_window.h"
#include "hal/window_decl.h"

namespace mister::hal {

template <class Layout>
struct TakenWindow {
    RegisterWindow<Layout> regs;
    WindowClaim claim;
};

}
