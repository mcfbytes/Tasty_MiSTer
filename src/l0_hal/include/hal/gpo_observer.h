// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/gpo_word.h"

namespace mister::hal {

class IGpoObserver {
public:
    virtual ~IGpoObserver() = default;

    virtual void on_store(GpoWord output) noexcept = 0;

protected:
    IGpoObserver() = default;
    IGpoObserver(const IGpoObserver&) = default;
    IGpoObserver& operator=(const IGpoObserver&) = default;
};

}
