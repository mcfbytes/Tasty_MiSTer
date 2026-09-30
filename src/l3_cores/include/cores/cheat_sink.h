// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/error.h"

namespace mister::cores {

class ICheatSink {
public:
    virtual ~ICheatSink() = default;

    [[nodiscard]] virtual Ex<void> apply(std::span<const std::uint8_t> table,
                                         std::uint32_t unit) = 0;

protected:
    ICheatSink() = default;
    ICheatSink(const ICheatSink&) = default;
    ICheatSink& operator=(const ICheatSink&) = default;
};

}  // namespace mister::cores
