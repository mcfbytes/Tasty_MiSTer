// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"

namespace mister::hal {

class ISpiOutputObserver {
public:
    virtual ~ISpiOutputObserver() = default;

    virtual void on_store(SpiOutput output) noexcept = 0;

protected:
    ISpiOutputObserver() = default;
    ISpiOutputObserver(const ISpiOutputObserver&) = default;
    ISpiOutputObserver& operator=(const ISpiOutputObserver&) = default;
};

}
