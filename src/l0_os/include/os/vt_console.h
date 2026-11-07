// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/error.h"
#include "os/types.h"

namespace mister::os {

class IVtConsole {
public:
    virtual ~IVtConsole() = default;

    [[nodiscard]] virtual std::optional<VtNumber> active() noexcept = 0;

    [[nodiscard]] virtual Ex<void> activate(VtNumber vt) noexcept = 0;

    virtual void blank(const char* tty) noexcept = 0;

    virtual void restore_text(const char* tty) noexcept = 0;

    [[nodiscard]] virtual std::optional<bool> text_mode(VtNumber vt) noexcept = 0;

    virtual void graphics(VtNumber vt) noexcept = 0;

    virtual void text(VtNumber vt) noexcept = 0;

    virtual void cursor(VtNumber vt, bool shown) noexcept = 0;
};

}
