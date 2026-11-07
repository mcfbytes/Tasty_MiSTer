// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/types.h"

namespace mister::svc {

struct PackedKeys {
    EvdevCode lo{};
    EvdevCode hi{};

    [[nodiscard]] static constexpr PackedKeys lone(EvdevCode code) noexcept {
        return PackedKeys{code, EvdevCode{}};
    }

    friend constexpr bool operator==(PackedKeys, PackedKeys) = default;
};

static_assert(sizeof(PackedKeys) == 4, "a packed pair is one 32-bit joy-map word");
static_assert(sizeof(PackedKeys) == 2 * sizeof(EvdevCode), "two 16-bit halves, never widened");

}
