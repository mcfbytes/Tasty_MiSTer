// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

namespace mister::cores::neogeo {

class IRomShape {
public:
    virtual ~IRomShape() = default;
    virtual void transform(std::span<std::uint8_t> piece) noexcept = 0;
};

}
