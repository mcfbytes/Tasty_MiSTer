// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::app {

class LwLendKey {
    LwLendKey() = default;
    friend class CoreBuilder;
    friend struct LwBlockLendTestAccess;
};

}
