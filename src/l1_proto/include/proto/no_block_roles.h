// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/block_roles.h"

namespace mister::proto {

class NoBlockRoles final : public IBlockRoles {
public:
    [[nodiscard]] BlockBacking resolve(SlotServer) noexcept override { return {}; }
};

}
