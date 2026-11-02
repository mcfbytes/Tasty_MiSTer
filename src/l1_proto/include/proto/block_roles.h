// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/block_backing.h"

namespace mister::proto {

class IBlockRoles {
public:
    virtual ~IBlockRoles() = default;

    [[nodiscard]] virtual BlockBacking resolve(SlotServer kind) noexcept = 0;

protected:
    IBlockRoles() = default;
    IBlockRoles(const IBlockRoles&) = default;
    IBlockRoles& operator=(const IBlockRoles&) = default;
};

}
