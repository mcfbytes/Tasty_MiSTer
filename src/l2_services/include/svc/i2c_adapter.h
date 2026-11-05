// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "os/types.h"

namespace mister::svc::adv7513 {

class II2cAdapter {
public:
    virtual ~II2cAdapter() = default;

    [[nodiscard]] virtual Ex<std::uint8_t> probe(unsigned bus, os::I2cAddr addr) = 0;

    [[nodiscard]] virtual Ex<void> open(unsigned bus, os::I2cAddr addr) = 0;

    [[nodiscard]] virtual Ex<std::uint8_t> read(os::I2cAddr addr, os::I2cReg reg) = 0;
    [[nodiscard]] virtual Ex<void> write(os::I2cAddr addr, os::I2cReg reg, std::uint8_t val) = 0;

protected:
    II2cAdapter() = default;
    II2cAdapter(const II2cAdapter&) = default;
    II2cAdapter& operator=(const II2cAdapter&) = default;
};

}
