// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/error.h"
#include "infra/seat.h"
#include "os/vt_console.h"

namespace mister::os {

class UiVtConsole final : public IVtConsole {
    TASTY_SEAT_RESIDENT(Ui);

public:
    [[nodiscard]] std::optional<int> active() noexcept override;
    [[nodiscard]] Ex<void> activate(int vt) noexcept override;
    void blank(const char* tty) noexcept override;
    void restore_text(const char* tty) noexcept override;
    [[nodiscard]] std::optional<bool> text_mode(int vt) noexcept override;
    void graphics(int vt) noexcept override;
    void text(int vt) noexcept override;
    void cursor(int vt, bool shown) noexcept override;
};

}
