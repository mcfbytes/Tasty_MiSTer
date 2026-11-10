// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "proto/types.h"

namespace mister::cores {
class Core;
}

namespace mister::app {

class ICoreOptionActs {
public:
    virtual ~ICoreOptionActs() = default;

    [[nodiscard]] virtual Ex<void> set_dip_counted(
        infra::OptRef<cores::Core> core,
        proto::DipRowIndex row,
        proto::DipChoice choice
    ) noexcept = 0;
    [[nodiscard]] virtual Ex<void> set_option_counted(
        infra::OptRef<cores::Core> core,
        proto::OptionRowIndex row,
        proto::FieldValue choice
    ) noexcept = 0;

protected:
    ICoreOptionActs() = default;
    ICoreOptionActs(const ICoreOptionActs&) = default;
    ICoreOptionActs& operator=(const ICoreOptionActs&) = default;
};

}
