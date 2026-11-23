// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "infra/prelude.h"
#include "svc/nic_window.h"

namespace mister::svc {

class INicWindowSource {
public:
    virtual ~INicWindowSource() = default;

    [[nodiscard]] virtual Ex<void> open() = 0;

    [[nodiscard]] virtual INicWindow& window() noexcept = 0;
    virtual void close() noexcept = 0;

protected:
    INicWindowSource() = default;
    INicWindowSource(const INicWindowSource&) = default;
    INicWindowSource& operator=(const INicWindowSource&) = default;
};

}
