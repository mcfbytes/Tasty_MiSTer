// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "infra/error.h"
#include "infra/prelude.h"

namespace mister::cores {

class IShareHost {
public:
    virtual ~IShareHost() = default;

    [[nodiscard]] virtual Ex<void> bind(std::span<std::byte> page) = 0;

    virtual void unbind() noexcept = 0;

    virtual void reset() noexcept = 0;

    virtual void serve() noexcept = 0;

protected:
    IShareHost() = default;
};

}
