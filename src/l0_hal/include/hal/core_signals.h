// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "hal/core_capabilities.h"
#include "hal/types.h"
#include "infra/prelude.h"

namespace mister::hal {

class ICoreSignals {
public:
    virtual ~ICoreSignals() = default;
    ICoreSignals() = default;
    ICoreSignals(const ICoreSignals&) = delete;
    ICoreSignals& operator=(const ICoreSignals&) = delete;

protected:
    ICoreSignals(ICoreSignals&&) = default;
    ICoreSignals& operator=(ICoreSignals&&) = default;

public:
    [[nodiscard]] virtual Ex<CoreTypeByte> identify() = 0;

    virtual CoreCapabilities latch_capabilities() = 0;

    virtual void set_core_reset(bool asserted) = 0;

    virtual void clear_signals() {}
};

}
