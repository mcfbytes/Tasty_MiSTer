// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "os/vt_console.h"

namespace mister::os {

class LinuxVtConsole final : public IVtConsole {
    TASTY_SEAT_RESIDENT(Launcher);

public:
    [[nodiscard]] std::optional<int> active() noexcept override;
    [[nodiscard]] Ex<void> activate(int vt) noexcept override;
    void blank(const char* tty) noexcept override;
    void restore_text(const char* tty) noexcept override;
};

}  // namespace mister::os
