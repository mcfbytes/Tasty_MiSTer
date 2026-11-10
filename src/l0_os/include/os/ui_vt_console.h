// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "os/vt_console.h"

namespace mister::os {

class UiVtConsole final : public IVtMode {
    TASTY_SEAT_RESIDENT(Ui);

public:
    [[nodiscard]] std::optional<VtNumber> active() noexcept override;
    [[nodiscard]] std::optional<bool> text_mode(VtNumber vt) noexcept override;
    void graphics(VtNumber vt) noexcept override;
    void text(VtNumber vt) noexcept override;
    void cursor(VtNumber vt, bool shown) noexcept override;
};

}
