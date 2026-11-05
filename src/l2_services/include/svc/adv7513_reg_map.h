// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "os/types.h"

namespace mister::svc::adv7513 {

class IRegMap {
public:
    virtual ~IRegMap() = default;

    [[nodiscard]] virtual Ex<std::uint8_t> read(os::I2cReg reg) = 0;
    [[nodiscard]] virtual Ex<void> write(os::I2cReg reg, std::uint8_t val) = 0;

protected:
    IRegMap() = default;
    IRegMap(const IRegMap&) = default;
    IRegMap& operator=(const IRegMap&) = default;
};

}
