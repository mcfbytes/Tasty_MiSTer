// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"

namespace mister::os {

class IKeyInjector {
public:
    virtual ~IKeyInjector() = default;

    [[nodiscard]] virtual Ex<void> open() noexcept = 0;

    virtual void key(std::uint16_t code, bool down) noexcept = 0;
};

}  // namespace mister::os
