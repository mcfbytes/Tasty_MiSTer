// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"

namespace mister::svc::adv7513 {

class II2cAdapter {
public:
    virtual ~II2cAdapter() = default;

    virtual Ex<std::uint8_t> probe(unsigned bus, std::uint8_t addr) = 0;

    virtual Ex<void> open(unsigned bus, std::uint8_t addr) = 0;

    virtual Ex<std::uint8_t> read(std::uint8_t addr, std::uint8_t reg) = 0;
    virtual Ex<void> write(std::uint8_t addr, std::uint8_t reg, std::uint8_t val) = 0;

protected:
    II2cAdapter() = default;
    II2cAdapter(const II2cAdapter&) = default;
    II2cAdapter& operator=(const II2cAdapter&) = default;
};

}  // namespace mister::svc::adv7513
