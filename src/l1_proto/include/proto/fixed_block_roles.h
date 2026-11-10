// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/block_roles.h"

namespace mister::proto {

class FixedBlockRoles final : public IBlockRoles {
public:
    explicit FixedBlockRoles(BlockBacking fixed) noexcept : fixed_(fixed) {}

    [[nodiscard]] BlockBacking resolve(SlotServer kind) noexcept override {
        return fixed_.kind() == kind ? fixed_ : BlockBacking{};
    }

private:
    BlockBacking fixed_;
};

}
