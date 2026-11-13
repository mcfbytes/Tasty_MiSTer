// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "os/vt_console.h"

namespace mister::os {

class LinuxVtConsole final : public IVtSwitch {
    TASTY_SEAT_RESIDENT(Launcher);

public:
    [[nodiscard]] std::optional<VtNumber> active() noexcept override;
    [[nodiscard]] Ex<void> activate(VtNumber vt) noexcept override;
    void blank(VtNumber vt) noexcept override;
    void restore_text(VtNumber vt) noexcept override;
};

}
