// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

namespace mister::cores {
class Core;
}

namespace mister::app {

struct CoreDeleter {

    CoreDeleter& operator=(std::default_delete<cores::Core>) noexcept { return *this; }
    void operator()(cores::Core* p) const noexcept;
};

using CorePtr = std::unique_ptr<cores::Core, CoreDeleter>;

}  // namespace mister::app
