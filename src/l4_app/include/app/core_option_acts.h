// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/opt_ref.h"

namespace mister::cores {
class Core;
}

namespace mister::app {

class ICoreOptionActs {
public:
    virtual ~ICoreOptionActs() = default;

    [[nodiscard]] virtual bool set_dip_counted(
        infra::OptRef<cores::Core> core,
        std::uint8_t row,
        std::uint32_t choice
    ) noexcept = 0;
    [[nodiscard]] virtual bool set_option_counted(
        infra::OptRef<cores::Core> core,
        std::uint8_t row,
        std::uint8_t choice
    ) noexcept = 0;

protected:
    ICoreOptionActs() = default;
    ICoreOptionActs(const ICoreOptionActs&) = default;
    ICoreOptionActs& operator=(const ICoreOptionActs&) = default;
};

}
