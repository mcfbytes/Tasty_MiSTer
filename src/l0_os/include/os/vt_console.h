// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/error.h"

namespace mister::os {

class IVtConsole {
public:
    virtual ~IVtConsole() = default;

    [[nodiscard]] virtual std::optional<int> active() noexcept = 0;

    [[nodiscard]] virtual Ex<void> activate(int vt) noexcept = 0;

    virtual void blank(const char* tty) noexcept = 0;

    virtual void restore_text(const char* tty) noexcept = 0;
};

}  // namespace mister::os
