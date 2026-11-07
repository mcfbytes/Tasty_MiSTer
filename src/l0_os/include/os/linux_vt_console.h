// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "os/vt_console.h"

namespace mister::os {

class LinuxVtConsole final : public IVtConsole {
    TASTY_SEAT_RESIDENT(Launcher);

public:
    [[nodiscard]] std::optional<VtNumber> active() noexcept override;
    [[nodiscard]] Ex<void> activate(VtNumber vt) noexcept override;
    void blank(const char* tty) noexcept override;
    void restore_text(const char* tty) noexcept override;
    [[nodiscard]] std::optional<bool> text_mode(VtNumber vt) noexcept override;
    void graphics(VtNumber vt) noexcept override;
    void text(VtNumber vt) noexcept override;
    void cursor(VtNumber vt, bool shown) noexcept override;
};

}
