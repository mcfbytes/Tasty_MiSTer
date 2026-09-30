// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"

namespace mister::svc::adv7513 {

class IRegMap {
public:
    virtual ~IRegMap() = default;

    virtual Ex<std::uint8_t> read(std::uint8_t reg) = 0;
    virtual Ex<void> write(std::uint8_t reg, std::uint8_t val) = 0;

protected:
    IRegMap() = default;
    IRegMap(const IRegMap&) = default;
    IRegMap& operator=(const IRegMap&) = default;
};

}  // namespace mister::svc::adv7513
