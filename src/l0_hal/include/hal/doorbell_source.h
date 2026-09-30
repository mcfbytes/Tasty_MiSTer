// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "hal/cause_reg.h"
#include "hal/register_window.h"
#include "hal/types.h"
#include "os/types.h"
#include "os/uio_handle.h"

namespace mister::hal {

class IDoorbellSource {
public:
    virtual ~IDoorbellSource() = default;

    [[nodiscard]] virtual Ex<os::UioHandle> open_line(os::UioLine line) = 0;
    [[nodiscard]] virtual Ex<RegisterWindow<CauseReg>> map_cause(LwOffset base) = 0;

protected:
    IDoorbellSource() = default;
    IDoorbellSource(const IDoorbellSource&) = default;
    IDoorbellSource& operator=(const IDoorbellSource&) = default;
};

}  // namespace mister::hal
